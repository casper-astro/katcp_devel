kcs_export() {
  if [ $# -lt 1 ] ; then
    exit 1
  fi

  kcpcmd -s localhost:7147 -k -i -r setenv $1 $2
}

ip_to_hex() {
  if [ $# -lt 1 ] ; then
    return 1
  fi

  ip=$1

  hexip=$(echo $ip | tr '.' \\n | (echo obase=16 ; r=0 ; while read line ; do r=$[256*$r+$line] ; done ; echo $r ) | bc )

  echo "ip $ip is $hexip"

  export hexip
}

port_to_hex() {
  if [ $# -lt 1 ] ; then
    return 1
  fi

  port=$1

  hexport=$(echo -e "obase=16\n${port}" | bc )

  echo "port $port is $hexport"

  export hexport
}

detect_reg_set () {
  if [ $# -lt 2 ] ; then
    return 1
  fi

  reg=$1
  shift

  if kcppar -q -i -s $* -x wordread $reg 0  ; then 
    return 0
  fi

  return 1
}

disable_bf_count () {
  kcpmsg -l info "beamformer logic disabled"
  kcs_export KCS_BF_COUNT 0
}

detect_bf_count () {
  if [ $# -lt 1 ] ; then
    kcs_export KCS_BF_COUNT 0
    return 1
  fi

  i=0

  while detect_reg_set bf${i}_dest $*; do
    if [ $i -gt 8 ] ; then
      kcpmsg -l error "unreasonably large number of beamformers $i"
      kcs_export KCS_BF_COUNT 0
      return 1;
    fi
    kcs_export KCS_BF${i}_RX 0.0.0.0:0
    i=$[i+1]
  done

  kcpmsg -l info "system appears to have ${i} beams"

  kcs_export KCS_BF_COUNT $i
}

set_bf_rx () {
  if [ $# -lt 4 ] ; then
    return 1
  fi

  if [ "${1:0:2}" != "bf" ] ; then
    return 1
  fi

  beam=${1:2}

  if [ $beam -ge ${KCS_BF_COUNT} ] ; then
    return 1
  fi

  ip=$2
  port=$3

  ip_to_hex $ip
  port_to_hex $port

  shift 3

  if kcppar -i -s $* -x wordwrite bf${beam}_dest 1 0x${hexport} -x wordwrite bf${beam}_dest 0 0x${hexip} ; then
    kcs_export KCS_BF${beam}_RX ${ip}:${port}
  fi
}
