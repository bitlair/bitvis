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

  conf.check(lib='pthread', uselib_store='pthread', mandatory=False)
  conf.check(lib='m', uselib_store='m', mandatory=False)
  conf.check(lib='jack', uselib_store='jack')

  conf.check(function_name='clock_gettime', header_name='time.h', mandatory=False)
  conf.check(function_name='clock_gettime', header_name='time.h', lib='rt', uselib_store='rt', mandatory=False,
             msg='Checking for clock_gettime in librt')

  conf.write_config_header('config.h')

def build(bld):
  bld.program(source='src/main.cpp',
              use=['m','pthread','rt', 'jack'],        
              includes='./src',
              cxxflags='-Wall -g -DUTILNAMESPACE=BitVisUtil',
              target='bitvis')

