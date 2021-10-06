#!/usr/bin/perl -w

package rts_util;

use strict;
use warnings;

use base 'Exporter';

our @EXPORT = qw(R_NAME R_INIT R_CASES R_RUN R_CHECKS R_TYPE R_EXPECT R_TYPE_DBSELECT R_TYPE_CMD);

use constant R_NAME	=> "name";
use constant R_INIT	=> "init";
use constant R_CASES	=> "cases";
use constant R_RUN	=> "run";
use constant R_CHECKS	=> "checks";
use constant R_TYPE	=> "type";
use constant R_EXPECT	=> "expect";
use constant R_DESCR	=> "descr";

use constant R_TYPE_DBSELECT	=> "dbselect";
use constant R_TYPE_CMD		=> "cmd";

1;
