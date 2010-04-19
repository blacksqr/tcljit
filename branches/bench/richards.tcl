# Tcl version of Martin Richards' benchmark program.
#
# Implemented by Guilherme Polo, based on both the C version by M. J. Jordan and the Pascal
# version by J. M. Bishop.
#
# 19/04/2010 - Initial version
#
# To run this benchmark: tclsh richards.tcl
#

set Count		10000
set Qpktcountval	23246
set Holdcountval	9297
#set Count		1000000
#set Qpkcountval	2326410
#set Holdcountval	930563

set BUFSIZE		3
set I_IDLE		1
set I_WORK		2
set I_HANDLERA		3
set I_HANDLERB		4
set I_DEVA		5
set I_DEVB		6
set PKTBIT		1
set WAITBIT		2
set HOLDBIT		4
set NOTHOLDBIT		0xFFFB

set S_RUN		0
set S_RUNPKT		1
set S_WAIT		2
set S_WAITPKT		3
set S_HOLD		4
set S_HOLDPKT		5
set S_HOLDWAIT		6
set S_HOLDWAITPKT	7

set K_DEV 		1000
set K_WORK 		1001

set taskmax		10
set tasktab		[list 0 0 0 0 0 0 0 0 0 0]
set tasklist		0

set tcb			0
set taskid		0
set qpktcount		0
set holdcount		0

set tracing		0
set layout 		0

set buf0 [list]
for {set i 0} {$i <= $BUFSIZE} {incr i} {
	lappend buf0 0
}


namespace eval task {
	variable counter 0
	variable tasks [dict create]
}

proc task::new {id pri wkq state func v1 v2} {
	global tasktab tasklist
	variable counter
	variable tasks

	incr counter

	lset tasktab $id $counter
	dict set tasks $counter link $tasklist
	dict set tasks $counter id $id
	dict set tasks $counter pri $pri
	dict set tasks $counter wkq $wkq
	dict set tasks $counter state $state
	dict set tasks $counter fn $func
	switch $func {
		idlefn {
			dict set tasks $counter hh $v1
			dict set tasks $counter cnt $v2
		}
		workfn {
			dict set tasks $counter sender $v1
			dict set tasks $counter data $v2
		}
		handlerfn {
			dict set tasks $counter wpktag $v1
			dict set tasks $counter dpktag $v2
		}
		devfn {
			dict set tasks $counter workdone $v1
		}
		default {
			error "Invalid func name '$func'"
		}
	}
	set tasklist $counter

	return $counter
}

proc task::dump {tag} {
	variable tasks
	return [dict get $tasks $tag]
}

proc task::setkv {tag key value} {
	variable tasks
	dict set tasks $tag $key $value
}

proc task::getk {tag key} {
	variable tasks
	return [dict get $tasks $tag $key]
}


namespace eval packet {
	variable counter 0
	variable packets [dict create]
}

proc packet::new {link id kind} {
	variable counter
	variable packets
	global buf0

	incr counter

	dict set packets $counter link $link
	dict set packets $counter id $id
	dict set packets $counter kind $kind
	dict set packets $counter a1 0
	dict set packets $counter a2 $buf0
	
	return $counter
}

proc packet::dump {tag} {
	variable packets
	return [dict get $packets $tag]
}

proc packet::getk {tag key} {
	variable packets
	return [dict get $packets $tag $key]
}

proc packet::setkv {tag key value} {
	variable packets
	dict set packets $tag $key $value
}

proc packet::append {pt1 pt2} {
	packet::setkv $pt1 link 0

	if {!$pt2} {
		return $pt1
	}

	set currpt $pt2
	set next [packet::getk $pt2 link]
	while {$next} {
		set currpt $next
		set next [packet::getk $currpt link]
	}
	packet::setkv $currpt link $pt1

	return $pt2
}


proc createtask {id pri wkq state func v1 {v2 {}}} {
	task::new $id $pri $wkq $state $func $v1 $v2
}

proc pkt {link id kind} {
	return [packet::new $link $id $kind]
}


proc findtcb {id} {
	global tasktab taskmax

	if {$id > 0 && $id < $taskmax} {
		return [lindex $tasktab $id]
	}
	error "Bad task id '$id'"
}

proc qpkt {ptag} {
	global qpktcount taskid tcb
	global PKTBIT

	set t [findtcb [packet::getk $ptag id]]
	if {!$t} {
		return $t
	}

	incr qpktcount

	packet::setkv $ptag link 0
	packet::setkv $ptag id $taskid
	if {![task::getk $t wkq]} {
		task::setkv $t wkq $ptag
		task::setkv $t state [expr {[task::getk $t state] | $PKTBIT}]
		if {[task::getk $t pri] > [task::getk $tcb pri]} {
			return $t
		}
	} else {
		packet::append $ptag [task::getk $t wkq]
	}

	return $tcb
}


proc handlerfn {ptag} {
	global tcb
	global K_WORK BUFSIZE

	set wpktag [task::getk $tcb wpktag]
	set dpktag [task::getk $tcb dpktag]

	if {$ptag} {
		set pkt_kind [packet::getk $ptag kind]
		if {$pkt_kind == $K_WORK} {
			set wpktag [packet::append $ptag $wpktag]
			task::setkv $tcb wpktag $wpktag
		} else {
			set dpktag [packet::append $ptag $dpktag]
			task::setkv $tcb dpktag $dpktag
		}
	}

	if {$wpktag} {
		set count [packet::getk $wpktag a1]
		if {$count > $BUFSIZE} {
			task::setkv $tcb wpktag [packet::getk $wpktag link]
			return [qpkt $wpktag]
		}

		if {$dpktag} {
			task::setkv $tcb dpktag [packet::getk $dpktag link]
			packet::setkv $dpktag a1 [lindex [packet::getk $wpktag a2] $count]
			packet::setkv $wpktag a1 [expr {$count + 1}]
			return [qpkt $dpktag]
		}
	}

	return [wait]
}

proc wait {} {
	global tcb
	global WAITBIT

	task::setkv $tcb state [expr {[task::getk $tcb state] | $WAITBIT}]
	return $tcb
}

proc release {id} {
	global tcb
	global NOTHOLDBIT

	set t [findtcb $id]
	if {!$t} {
		return $t
	}

	task::setkv $t state [expr {[task::getk $t state] & $NOTHOLDBIT}]
	if {[task::getk $t pri] > [task::getk $tcb pri]} {
		return $t
	}
	return $tcb
}

proc holdself {} {
	global holdcount tcb
	global HOLDBIT

	incr holdcount
	task::setkv $tcb state [expr {[task::getk $tcb state] | $HOLDBIT}]
	return [task::getk $tcb link]
}

proc idlefn {ptag} {
	global tcb
	global I_DEVA I_DEVB

	task::setkv $tcb cnt [expr {[task::getk $tcb cnt] -1}]

	if {![task::getk $tcb cnt]} {
		return [holdself]
	}

	set hh [task::getk $tcb hh]
	if {($hh & 1) == 0} {
		task::setkv $tcb hh [expr {$hh >> 1}] 
		return [release $I_DEVA]
	} else {
		task::setkv $tcb hh [expr {($hh >> 1) ^ 0xD008}]
		return [release $I_DEVB]
	}
}

proc devfn {ptag} {
	global tcb tracing

	if {!$ptag} {
		set ptag [task::getk $tcb workdone]
		if {!$ptag} {
			return [wait]
		}
		task::setkv $tcb workdone 0
		return [qpkt $ptag]
	} else {
		task::setkv $tcb workdone $ptag 
		if {$tracing} {
			trace_ [format %c [packet::getk $ptag a1]]
		}
		return [holdself]
	}
}

proc workfn {ptag} {
	global tcb
	global I_HANDLERA I_HANDLERB BUFSIZE

	if {!$ptag} {
		return [wait]
	}

	task::setkv $tcb sender [expr $I_HANDLERA + $I_HANDLERB - [task::getk $tcb sender]]
	packet::setkv $ptag id [task::getk $tcb sender]
	packet::setkv $ptag a1 0
	set a2 [packet::getk $ptag a2]
	set count [task::getk $tcb data]
	for {set i 0} {$i <= $BUFSIZE} {incr i} {
		incr count
		if {$count > 26} {
			set count 1
		}
		# A = 65 in ascii
		lset a2 $i [expr {65 + $count - 1}]
	}
	task::setkv $tcb data $count
	packet::setkv $ptag a2 $a2

	return [qpkt $ptag] 
}


proc schedule {} {
	global tcb taskid tracing
	global S_WAITPKT S_RUN S_RUNPKT S_WAIT S_HOLD S_HOLDPKT S_HOLDWAIT S_HOLDWAITPKT 

	while {$tcb} {
		set state [task::getk $tcb state]
		set pkt_tag 0

		if {$state == $S_WAITPKT || $state == $S_RUN || $state == $S_RUNPKT} {
			if {$state == $S_WAITPKT} {
				set pkt_tag [task::getk $tcb wkq]
				set pkt_link [packet::getk $pkt_tag link]
				task::setkv $tcb wkq $pkt_link

				if {!$pkt_link} {
					task::setkv $tcb state $S_RUN
				} else {
					task::setkv $tcb state $S_RUNPKT
				}
			}
			set taskid [task::getk $tcb id]

			if {$tracing} { trace_ $taskid }

			set func [task::getk $tcb fn]
			set tcb [$func $pkt_tag]
			continue
		}


		if {$state == $S_WAIT || $state == $S_HOLD || $state == $S_HOLDPKT ||
			$state == $S_HOLDWAIT || $state == $S_HOLDWAITPKT} {

			set tcb [task::getk $tcb link]
			continue
		}

		return
	}
}

proc trace_ {char} {
	global layout

	incr layout -1
	if {$layout <= 0} {
		puts {}
		set layout 50
	}
	puts -nonewline $char
}


proc main {} {
	global I_DEVA I_DEVB I_IDLE I_WORK K_DEV S_RUN S_WAIT S_WAITPKT K_WORK Count
	global I_HANDLERA I_HANDLERB
	global Qpktcountval Holdcountval
	global tasktab tcb tasklist qpktcount holdcount layout

	puts "Bench mark starting"

	createtask $I_IDLE 0 0 $S_RUN idlefn 1 $Count

	set wkq [pkt 0 0 $K_WORK]
	set wkq [pkt $wkq 0 $K_WORK]
	createtask $I_WORK 1000 $wkq $S_WAITPKT workfn $I_HANDLERA 0

	set wkq [pkt 0 $I_DEVA $K_DEV]
	set wkq [pkt $wkq $I_DEVA $K_DEV]
	set wkq [pkt $wkq $I_DEVA $K_DEV]
	createtask $I_HANDLERA 2000 $wkq $S_WAITPKT handlerfn 0 0

	set wkq [pkt 0 $I_DEVB $K_DEV]
	set wkq [pkt $wkq $I_DEVB $K_DEV]
	set wkq [pkt $wkq $I_DEVB $K_DEV]
	createtask $I_HANDLERB 3000 $wkq $S_WAITPKT handlerfn 0 0

	createtask $I_DEVA 4000 0 $S_WAIT devfn 0
	createtask $I_DEVB 5000 0 $S_WAIT devfn 0

	set tcb $tasklist
	set qpktcount 0
	set holdcount 0
	set layout 0

	puts "Starting"

	schedule

	puts "\nfinished"
	puts "qpkt count = $qpktcount, holdcount = $holdcount"
	puts -nonewline "These results are "
	if {$qpktcount == $Qpktcountval && $holdcount == $Holdcountval} {
		puts "correct"
	} else {
		puts "incorrect"
	}
	puts "end of run"
}

puts [time main 1]
