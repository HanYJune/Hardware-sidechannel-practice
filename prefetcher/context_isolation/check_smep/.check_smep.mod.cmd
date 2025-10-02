savedcmd_check_smep.mod := printf '%s\n'   check_smep.o | awk '!x[$$0]++ { print("./"$$0) }' > check_smep.mod
