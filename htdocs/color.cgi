#!/usr/bin/perl -Tw

use strict;
use CGI;

my($cgi) = new CGI;

#print $cgi->header;
my($color) = "blue";
my($path) = $ENV{'PATH'};
foreach (sort keys %ENV) { 
  print $cgi->span("$_  =  $ENV{$_}\n"); 
  print $cgi->br; 
}
#$color = $cgi->param('color') if defined $cgi->param('color');

#print $cgi->start_html(-title => uc($color),
#                       -BGCOLOR => $color);
#print $cgi->h1("This is $path");
#print $cgi->end_html;
