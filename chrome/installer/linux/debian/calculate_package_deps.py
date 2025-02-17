#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Given a binary, uses dpkg-shlibdeps to check that its package dependencies
are satisfiable on all supported debian-based distros.
"""

import argparse
import json
import os
import re
import subprocess
import sys

import deb_version
import package_version_interval

parser = argparse.ArgumentParser()
parser.add_argument('binary')
parser.add_argument('sysroot')
parser.add_argument('arch')
parser.add_argument('dep_filename')
parser.add_argument('--distro-check', action='store_true')
args = parser.parse_args()

binary = os.path.abspath(args.binary)
sysroot = os.path.abspath(args.sysroot)
arch = args.arch
dep_filename = os.path.abspath(args.dep_filename)
distro_check = args.distro_check

cmd = ['dpkg-shlibdeps']
if arch == 'x64':
  cmd.extend(['-l%s/usr/lib/x86_64-linux-gnu' % sysroot,
              '-l%s/lib/x86_64-linux-gnu' % sysroot])
elif arch == 'x86':
  cmd.extend(['-l%s/usr/lib/i386-linux-gnu' % sysroot,
              '-l%s/lib/i386-linux-gnu' % sysroot])
elif arch == 'arm':
  cmd.extend(['-l%s/usr/lib/arm-linux-gnueabihf' % sysroot,
              '-l%s/lib/arm-linux-gnueabihf' % sysroot])
elif arch == 'mipsel':
  cmd.extend(['-l%s/usr/lib/mipsel-linux-gnu' % sysroot,
              '-l%s/lib/mipsel-linux-gnu' % sysroot])
elif arch == 'mips64el':
  cmd.extend(['-l%s/usr/lib/mips64el-linux-gnuabi64' % sysroot,
              '-l%s/lib/mips64el-linux-gnuabi64' % sysroot])
else:
  print 'Unsupported architecture ' + arch
  sys.exit(1)
cmd.extend(['-l%s/usr/lib' % sysroot, '-O', '-e', binary])

proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                        cwd=sysroot)
exit_code = proc.wait()
(stdout, stderr) = proc.communicate()
if exit_code != 0:
  print 'dpkg-shlibdeps failed with exit code ' + str(exit_code)
  print 'stderr was ' + stderr
  sys.exit(1)

deps_str = stdout.replace('shlibs:Depends=', '').replace('\n', '')
deps = deps_str.split(', ')
interval_sets = []
if deps_str != '':
  for dep in deps:
    interval_sets.append(package_version_interval.parse_interval_set(dep))

script_dir = os.path.dirname(os.path.abspath(__file__))
deps_file = os.path.join(script_dir, 'dist_package_versions.json')
distro_package_versions = json.load(open(deps_file))

ret_code = 0
if distro_check:
  for distro in distro_package_versions:
    for interval_set in interval_sets:
      dep_satisfiable = False
      for interval in interval_set.intervals:
        package = interval.package
        if package not in distro_package_versions[distro]:
          continue
        distro_version = deb_version.DebVersion(
            distro_package_versions[distro][package])
        if interval.contains(distro_version):
          dep_satisfiable = True
          break
      if not dep_satisfiable:
        print >> sys.stderr, (
            'Dependency %s not satisfiable on distro %s caused by binary %s' % (
                interval_set.formatted(), distro, os.path.basename(binary)))
        ret_code = 1
if ret_code == 0:
  with open(dep_filename, 'w') as dep_file:
    lines = [interval_set.formatted() + '\n'
           for interval_set in interval_sets]
    dep_file.write(''.join(sorted(lines)))
sys.exit(ret_code)
