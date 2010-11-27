ciso is a simple commandline utility to compress PSP iso files.

This package is a (currently incomplete) port of a package provided
by Ubuntu: http://packages.ubuntu.com/natty/ciso

# Usage

To decompress a cso file:

  ciso 0 infile.cso outfile.iso

To compress an iso file:

  ciso level infile.iso outfile.cso

where level ranges from 1 (fast, poor compression) to 9 (slow, high
compression).

# WARNING

Do Not Trust Your Data To This

While it appears that iso -> cso -> iso roundtrips are lossless, the
generated cso is broken and existing csos that I have are not
recognized by the executable.

Given the compilation warnings I get I am assuming it's buggy due to
differences between Linux and MacOS libraries.
