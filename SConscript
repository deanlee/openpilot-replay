Import('env', 'arch', 'common', 'messaging', 'visionipc', 'cereal')

is_darwin = (arch == "Darwin")
frameworks = ['OpenCL'] if is_darwin else []
opencl = [] if is_darwin else ['OpenCL']

src = Glob('src/*.cc')
exclude = ['main.cc']
if is_darwin:
    exclude.append('qcom_decoder.cc')

for f in exclude:
    src.remove(File(f'src/{f}'))

# Use explicit .so names to force dynamic linking — avoids picking up system
# libssl.a/libcrypto.a which would conflict with libcurl's system libssl.so.
ssl_libs = [':libssl.so', ':libcrypto.so'] if not is_darwin else ['ssl', 'crypto']
libs = [common, messaging, cereal, visionipc] + ssl_libs + ['pthread', 'zmq',
        'avutil', 'avcodec', 'avformat', 'swscale', 'bz2', 'zstd', 'curl', 'ncurses'] + opencl

replay_lib = env.Library("replay", src, LIBS=libs, FRAMEWORKS=frameworks)
replay_bin = env.Program("replay", ["src/main.cc"], LIBS=[replay_lib] + libs, FRAMEWORKS=frameworks)

# Return objects so the parent can use them (e.g., for installation or aliases)
Return('replay_lib')
