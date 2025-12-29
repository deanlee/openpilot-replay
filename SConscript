Import('env', 'arch', 'common', 'messaging', 'visionipc', 'cereal')

replay_env = env.Clone()
replay_env['CCFLAGS'] += ['-Wno-deprecated-declarations']

base_frameworks = []
base_libs = [common, messaging, cereal, visionipc, 'm', 'ssl', 'crypto', 'pthread', 'zmq']

if arch == "Darwin":
  base_frameworks.append('OpenCL')
else:
  base_libs.append('OpenCL')

replay_lib_src = ["src/replay.cc", "src/consoleui.cc", "src/camera.cc", "src/filereader.cc", "src/logreader.cc", "src/framereader.cc",
                  "src/route.cc", "src/util.cc", "src/seg_mgr.cc", "src/timeline.cc", "src/api.cc"]
if arch != "Darwin":
  replay_lib_src.append("src/qcom_decoder.cc")
replay_lib = replay_env.Library("replay", replay_lib_src, LIBS=base_libs, FRAMEWORKS=base_frameworks)
Export('replay_lib')
replay_libs = [replay_lib, 'avutil', 'avcodec', 'avformat', 'bz2', 'zstd', 'curl', 'ncurses'] + base_libs
replay_env.Program("replay", ["src/main.cc"], LIBS=replay_libs, FRAMEWORKS=base_frameworks)
