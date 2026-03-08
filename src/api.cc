
#include "api.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <chrono>
#include <iostream>
#include <memory>

#include "common/params.h"
#include "common/version.h"
#include "hardware.h"

namespace CommaApi2 {

static constexpr char base64url_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";

std::string base64url_encode(const std::string &in) {
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64url_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) {
    out.push_back(base64url_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  }
  return out;
}

EVP_PKEY *get_rsa_private_key() {
  static std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> rsa_private(nullptr, EVP_PKEY_free);
  if (!rsa_private) {
    FILE *fp = fopen(Path::rsa_file().c_str(), "rb");
    if (!fp) {
      std::cerr << "No RSA private key found, please run manager.py or registration.py" << std::endl;
      return nullptr;
    }
    rsa_private.reset(PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr));
    fclose(fp);
  }
  return rsa_private.get();
}

std::string rsa_sign(const std::string &data) {
  EVP_PKEY *private_key = get_rsa_private_key();
  if (!private_key) return {};

  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> mdctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!mdctx) return {};

  if (EVP_DigestSignInit(mdctx.get(), nullptr, EVP_sha256(), nullptr, private_key) != 1 ||
      EVP_DigestSignUpdate(mdctx.get(), data.data(), data.size()) != 1)
    return {};

  size_t sig_len = 0;
  if (EVP_DigestSignFinal(mdctx.get(), nullptr, &sig_len) != 1) return {};

  std::string sig(sig_len, '\0');
  if (EVP_DigestSignFinal(mdctx.get(), reinterpret_cast<unsigned char *>(sig.data()), &sig_len) != 1) return {};
  sig.resize(sig_len);
  return sig;
}

std::string create_jwt(const json &extra, int exp_time) {
  auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::string dongle_id = Params().get("DongleId");

  json header = {{"alg", "RS256"}};
  json payload = {
      {"identity", dongle_id},
      {"iat", now},
      {"nbf", now},
      {"exp", now + exp_time},
  };
  if (!extra.is_null()) {
    payload.update(extra);
  }

  std::string jwt = base64url_encode(header.dump()) + '.' +
                    base64url_encode(payload.dump());
  return jwt + "." + base64url_encode(rsa_sign(jwt));
}

std::string create_token(bool use_jwt, const json &payloads, int expiry) {
  if (use_jwt) {
    return create_jwt(payloads, expiry);
  }

  std::string token_json = util::read_file(util::getenv("HOME") + "/.comma/auth.json");
  try {
    return json::parse(token_json).value("access_token", "");
  } catch (const json::parse_error &e) {
    std::cerr << "Error parsing auth.json: " << e.what() << std::endl;
    return "";
  }
}

std::string httpGet(const std::string &url, long *response_code) {
  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
  if (!curl) return {};

  std::string readBuffer;
  const std::string token = create_token(!Hardware::PC());

  curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION,
                   +[](char *data, size_t size, size_t nmemb, std::string *buf) -> size_t {
                     buf->append(data, size * nmemb);
                     return size * nmemb;
                   });
  curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &readBuffer);
  curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);

  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
      curl_slist_append(nullptr, "User-Agent: openpilot-" COMMA_VERSION), curl_slist_free_all);
  if (!token.empty()) {
    headers.reset(curl_slist_append(headers.release(), ("Authorization: JWT " + token).c_str()));
  }
  curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

  CURLcode res = curl_easy_perform(curl.get());
  if (response_code) {
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, response_code);
  }

  return res == CURLE_OK ? readBuffer : std::string{};
}

}  // namespace CommaApi2
