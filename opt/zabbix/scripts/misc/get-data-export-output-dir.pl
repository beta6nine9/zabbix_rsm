#!/usr/bin/env perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";

use RSM;

print(get_data_export_output_dir(), "\n");
