#!/bin/bash

#workingdir=~/work/katcp-git
workingdir=~/katcp-git

#hostsfile=$workingdir/kcs/scripts/hosts
hostfile=/etc/hosts
#kcpcmd="$workingdir/cmd/kcpcmd -q -s localhost:7147 roach add"
kcpcmd=echo

grep roach.*x $hostsfile | tr -s \  | (while read myip myhost; do $kcpcmd $(echo xport://$myhost:10001/ | cut -f1 -d.) $myip xport ; done)

#grep 'roach[[:digit:]]*\.' $hostsfile | tr -s \ | (while read myip myhost; do $kcpcmd $(echo katcp://$myhost:7147/ | cut -f1 -d.) $myip spare ; done)
