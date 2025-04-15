savedcmd_/home/vboxuser/vmsort_mod/vmsort.mod := printf '%s\n'   vmsort.o | awk '!x[$$0]++ { print("/home/vboxuser/vmsort_mod/"$$0) }' > /home/vboxuser/vmsort_mod/vmsort.mod
