#!/usr/bin/env perl

use FindBin;
use lib $FindBin::RealBin;

use strict;
use warnings;

use RSMSLV;

exit_if_running();

print("First instance!\n");
