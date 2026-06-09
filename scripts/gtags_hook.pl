use strict;
use warnings;
use File::Find;
find({
  no_chdir => 1,
  wanted => sub {
    return if -l;
    return unless -f;
    return unless /\.(?:h|hh|c|cc|sql)\z/;
    print "$_\n";
  },
}, '.');
