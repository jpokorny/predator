set confirm 0
set follow-fork-mode child
set pagination 0
set print pretty
run
echo \nbt:\n
bt
echo \nframe:\n
f 1
echo \ncode:\n
list
echo \n\n
