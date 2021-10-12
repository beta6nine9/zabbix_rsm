#!/usr/bin/env perl

use strict;
use warnings;

use FindBin;
use lib "$FindBin::RealBin/..";

use RSM;

print(get_sla_api_output_dir(), "\n");
