
make_mapping()
{
  pols=(x y)
  pol=$1

  if [ ! -f ${CORR_CONFIG}/wbc ] ; then
    kcs_error "unable to locate configuration wbc"
    return 1
  fi

  i=0
  declare -a roachposmap
  for name in $(grep ^servers_f.*roach ${CORR_CONFIG}/wbc | cut -f2 -d= | tr '[, ]' '\n' ) ; do
    roachposmap[$i]=$name
    i=$[i+1]
  done

  inp=$i

  : > ${MAIN_MAPPING}
  
  i=0
  while [ $i -lt $inp ] ; do
    j=0
    while [ $j -lt $pol ] ; do
      echo ${i}${pols[$j]}:${i}${pols[$j]}:${roachposmap[$i]} >> ${MAIN_MAPPING}
      j=$[j+1]
    done
    i=$[i+1]
  done

  return 0
}

update_aliases()
{
  for fullname in $(grep -l '^servers_f' ${CORR_CONFIG}/*) ; do
    name=${fullname##*/}
    kcs_info "updating config for $name"
    : > ${CORR_MAPPING}.${name}
    for roach in $(grep '^servers_f' ${fullname} | cut -f2 -d= | tr '[, ]' '\n') ; do
      grep ${roach} ${MAIN_MAPPING} | cut -f1 -d: | tr '\n' ',' >> ${CORR_MAPPING}.${name}
    done
  done
}

