savedcmd_ap.mod := printf '%s\n'   main.o | awk '!x[$$0]++ { print("./"$$0) }' > ap.mod
