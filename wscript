#! /usr/bin/env python
# encoding: utf-8

# the following two variables are used by the target "waf dist"
VERSION='0.0.1'
APPNAME='bitvis'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'

def options(opt):
  opt.load('compiler_cxx')
  opt.add_option('--disable-vlc', action='store_true', default=False, help='disable libvlc')

def configure(conf):
  conf.load('compiler_cxx')

  conf.env.DISABLE_VLC=conf.options.disable_vlc

  conf.check(header_name='jack/jack.h')
  conf.check(header_name='fftw3.h')
  conf.check(header_name='samplerate.h')
  conf.check(header_name='sys/ipc.h')
  conf.check(header_name='sys/shm.h')

  if not conf.options.disable_vlc:
    conf.check(header_name='vlc/vlc.h')

  conf.check(header_name='X11/Xlib.h', auto_add_header_name=True)
  conf.check(header_name='X11/extensions/Xrender.h')
  conf.check(header_name='X11/extensions/XShm.h')

  conf.check(lib='fftw3', uselib_store='fftw3')
  conf.check(lib='fftw3f', uselib_store='fftw3f')
  conf.check(lib='jack', uselib_store='jack')
  conf.check(lib='samplerate', uselib_store='samplerate')
  conf.check(lib='X11', uselib_store='X11')
  conf.check(lib='Xext', uselib_store='Xext')
  conf.check(lib='Xrender', uselib_store='Xrender')
  conf.check(lib='m', uselib_store='m', mandatory=False)
  conf.check(lib='pthread', uselib_store='pthread', mandatory=False)

  if not conf.options.disable_vlc:
    conf.check(lib='vlc', uselib_store='vlc')

  conf.check(function_name='clock_gettime', header_name='time.h', mandatory=False)
  conf.check(function_name='clock_gettime', header_name='time.h', lib='rt', uselib_store='rt', mandatory=False,
             msg='Checking for clock_gettime in librt')

  conf.write_config_header('config.h')

def build(bld):
  bld.program(source='src/bitvis/main.cpp\
                      src/bitvis/bitvis.cpp\
                      src/bitvis/jackclient.cpp\
                      src/bitvis/mpdclient.cpp\
                      src/bitvis/fft.cpp\
                      src/util/debugwindow.cpp\
                      src/util/log.cpp\
                      src/util/misc.cpp\
                      src/util/mutex.cpp\
                      src/util/timeutils.cpp\
                      src/util/condition.cpp\
                      src/util/tcpsocket.cpp\
                      src/util/thread.cpp',
              use=['m','pthread','rt', 'jack', 'fftw3', 'fftw3f', 'samplerate', 'X11', 'Xrender'],
              includes='./src',
              cxxflags='-Wall -g -DUTILNAMESPACE=BitVisUtil -O3',
              target='bitvis')

  bld.program(source='src/bitx11/main.cpp\
                      src/bitx11/bitx11.cpp\
                      src/util/debugwindow.cpp\
                      src/util/log.cpp\
                      src/util/misc.cpp\
                      src/util/mutex.cpp\
                      src/util/timeutils.cpp\
                      src/util/condition.cpp\
                      src/util/tcpsocket.cpp\
                      src/util/thread.cpp',
              use=['m', 'rt', 'X11', 'Xrender', 'Xext'],
              includes='./src',
              cxxflags='-Wall -g -DUTILNAMESPACE=BitX11Util',
              target='bitx11')

  if not bld.env.DISABLE_VLC:
    bld.program(source='src/bitvlc/main.cpp\
                        src/bitvlc/bitvlc.cpp\
                        src/util/condition.cpp\
                        src/util/debugwindow.cpp\
                        src/util/log.cpp\
                        src/util/misc.cpp\
                        src/util/mutex.cpp\
                        src/util/thread.cpp\
                        src/util/timeutils.cpp\
                        src/util/tcpsocket.cpp',
                use=['m', 'rt', 'X11', 'Xrender', 'Xext', 'vlc'],
                includes='./src',
                cxxflags='-Wall -g -DUTILNAMESPACE=BitVlcUtil',
                target='bitvlc')

