#!/usr/bin/python

import os, sys, textwrap, getopt
import subprocess as sp

prefix="    "
verbose=False
occ_device = "/dev/snsocc0"
test_file_suffix = ".vlt"
raw_file_marker = "raw"
path = "loopback"
repeat_count=1
pass_count=0
fail_count=0

# used to format output from occ_loopback for printing
wrapper = textwrap.TextWrapper(initial_indent=prefix, width=70,
                               break_long_words=False, replace_whitespace=False,
                               subsequent_indent=prefix)

# use splitlines so that I can indent each line of output with the prefix
def wrapped_print(in_string):
  for para in in_string.splitlines():
    print wrapper.fill(para)

def usage():
  f = open(path+'/README', 'r')
  print f.read()

def process_args(argv):
  global verbose
  global occ_device
  global repeat_count
  try:
    opts, args = getopt.getopt(argv, "hvd:r:")
  except getopt.GetoptError:
    usage()
    sys.exit(2)
  for opt, arg in opts:
    if opt == '-h':
      usage()
      sys.exit()
    elif opt == '-v':
      verbose=True
    elif opt == '-d':
      occ_device = arg
    elif opt == '-r':
      repeat_count = int(arg)

def main(argv):
  global pass_count
  global fail_count
  process_args(argv)
  dirs = os.listdir(path)
  for filename in dirs:
    i = len(filename)

    if filename.endswith(test_file_suffix, 0, i):
      cmd_line = ['../../tools/loopback/occ_loopback','-d',occ_device,'-i', path+"/"+filename,'-n']

      # append -r flag if file is to treated as raw mode
      # if filename.find(raw_file_marker, 0, i) != -1:
        # cmd_line.append('-r')

      for index in range(repeat_count):
        print ("%s" % filename),
        print '.' * (40-i),

        p = sp.Popen(cmd_line, stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE)
        output = p.communicate()

        if (p.returncode != 0):
          fail_count += 1
          print "error %d" % p.returncode
          print "    stdout: "
          wrapped_print(output[0])
          print "    stderr:"
          wrapped_print(output[1])
        else:
          pass_count += 1
          print "pass"
          if verbose:
            wrapped_print(output[0])

  print ""
  print "%d passed, %d failed" % (pass_count, fail_count)

if __name__ == "__main__":
  main(sys.argv[1:])
