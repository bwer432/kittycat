BEGIN { print "starting" }
NR == 1 { print "first line",$1; u=sprintf("cat ./kitty%s",$2); v=$3; s=system(u) }
NR > 1 { print "subsequent line",$1," with value ",$2 } 
/^$/ { print "got blank line" }
