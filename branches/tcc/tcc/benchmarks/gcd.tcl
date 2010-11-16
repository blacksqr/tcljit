proc gcd {a b} {
	if {!$b} {
		return -1
	}

	set aux [expr {$a % $b}]
	while {$aux} {
		set a $b
		set b $aux
		set aux [expr {$a % $b}]
	}
	return $b
}

if {$argc != 1} {
    puts "Usage: $argv0 n"
} else {
    set n [lindex $argv 0]

    for {set i 0} {$i < $n} {incr i} {
	    for {set j 0} {$j < $n} {incr j} {
		    gcd $i $j
	    }
    }
}

#puts [gcd 10 0]
#puts [gcd 18 24]
#puts [gcd 18 24]
#puts [gcd 1 300]
#puts [gcd 90 80]
#puts [gcd 10 0]
