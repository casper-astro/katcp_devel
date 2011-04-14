export CORR_CONFIG=/etc/corr/config
export KATCP_SERVER=localhost:1235

kcs_debug () {
  echo "#log debug $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
}

kcs_error () {
  echo "#log error $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
  return 1
}

kcs_info () {
  echo "#log info $(date +%s)000 script $(echo $1 | sed -e 's/ /\\_/g')"
}

kcs_arg_check () {
  if [ $1 -lt $2 ] ; then
    kcs_error "insufficient parameters"
    return 1
  fi
}

kcs_corr_log () {
  kcs_debug "retriving correlator logs"
  if kcpcmd -k -r get-log ; then
    kcpcmd -k -r clr-log
  fi
  return 0
}
