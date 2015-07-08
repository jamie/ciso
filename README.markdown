ciso is a simple commandline utility to compress PSP iso files.

This package is an OSX port of a package provided by Ubuntu: http://packages.ubuntu.com/search?keywords=ciso

# Installation

    make
    make install # (optional)

# Usage

To decompress a cso file:

    ciso 0 infile.cso outfile.iso

To compress an iso file:

    ciso level infile.iso outfile.cso

where level ranges from 1 (fast, poor compression) to 9 (slow, high
compression).
