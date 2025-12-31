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

libs = [common, messaging, cereal, visionipc, 'ssl', 'crypto', 'pthread', 'zmq',
        'avutil', 'avcodec', 'avformat', 'swscale', 'bz2', 'zstd', 'curl', 'ncurses'] + opencl

replay_lib = env.Library("replay", src, LIBS=libs, FRAMEWORKS=frameworks)
replay_bin = env.Program("replay", ["src/main.cc"], LIBS=[replay_lib] + libs, FRAMEWORKS=frameworks)

# Return objects so the parent can use them (e.g., for installation or aliases)
Return('replay_lib')
