from distutils.core import setup, Extension
import os.path

INCDIR=os.path.realpath('../lib')
LIBDIR=os.path.realpath('../lib')
module1 = Extension('occ',
                    include_dirs = [ INCDIR ],
                    libraries = [ 'occ' ],
                    library_dirs = [ LIBDIR ],
                    sources = [ 'occlibpy.c' ],
                    extra_link_args = [ '-Wl,-rpath,' + LIBDIR  ]
                   )

setup (name = 'OCClib',
       version = '0.1',
       description = 'Python OCC interface',
       ext_modules = [module1],
      )
