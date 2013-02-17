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

def configure(conf):
  conf.load('compiler_cxx')

  conf.check(header_name='jack/jack.h')
  conf.check(header_name='fftw3.h')
  conf.check(header_name='samplerate.h')

  conf.check(lib='fftw3', uselib_store='fftw3')
  conf.check(lib='fftw3f', uselib_store='fftw3f')
  conf.check(lib='jack', uselib_store='jack')
  conf.check(lib='samplerate', uselib_store='samplerate')
  conf.check(lib='m', uselib_store='m', mandatory=False)
  conf.check(lib='pthread', uselib_store='pthread', mandatory=False)

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
                      src/util/log.cpp\
                      src/util/misc.cpp\
                      src/util/mutex.cpp\
                      src/util/timeutils.cpp\
                      src/util/condition.cpp\
                      src/util/tcpsocket.cpp\
                      src/util/thread.cpp',
              use=['m','pthread','rt', 'jack', 'fftw3', 'fftw3f', 'samplerate'],
              includes='./src',
              cxxflags='-Wall -g -DUTILNAMESPACE=BitVisUtil',
              target='bitvis')

