#!/usr/bin/env python2.7

import os
import logging
from swarm import *
from swarm.ipython import magics
from IPython.config.loader import Config
from IPython.terminal.embed import InteractiveShellEmbed

# The SWARM Shell banner
swarm_shell_banner = """
DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDNNNNNNNN
DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDNNNNNN
DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD
DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD8D88DD8DDDDDDDDDDDDDDDDDDDDDDDDD
DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD=D8888888888DDDDDDDDDDDDDDDDDDDDDD
DDDDDDDDDDDDDDDDDDDDDDDDDDDDD~DD8888888:D8DDDDDDDD~:7888888888DDDDDDDDDDDDDDDDDD
DD8DDDDDDDDDDDDDDDDDDDDDDDD:DDD8888888OOOODDDDDD8==DNN88888888888D88DDDDDDDDDDDD
8888888888DDDDDDDDDDDDDDDD+DDDD888OOOOOZDD8ZZZ~DN?8DNND8888888888888888888888888
888888888888DD8DDDDDDDDDDD~D888888OOO?DDOZZZZZZ$DDDDOND8888888888888888888888888
8888888888888888888888888=:8888888ONDDZ$Z$$ZZZZM$$$D8DD8888888888888888888888888
8888888888888888888888888~:8OOZ8OO88OZZZ$$$$$$8$$777DD8888888888888888888888OOOO
8888888888888888888888888~~:ZOOZOOOZDZZZZ$77ID$7$77IOO88888888888O88OOO88OOOOOOO
OOOOO88888O88888888888888~::,ZZZZZZOZOZZZZ$?$ZD87I78O??88888OO8OOOOOOOOOOOOOOZZZ
OOOOOOOOOOOOOOOOOOO8888O8O::+,ZZZ$$$Z$ZZ$7:$Z$77D8?ZZ??=~OOOOOOOOOOOOOOOOOOOZZZZ
OZZZZZZZZOOOOOOOOOOOOOOOOOZ~~=,$Z$$7$$$$$N=I77????+O.=~::ZOOOOOOOOOZZOZZZZZZZZZZ
OOOOOOOZZZZZZZZZZZZZZOOOOOO~~==,$$$$7$Z$~$$$$$$I77OZ88++==ZZZZZZZZZZZZZZZZZZ$$$$
ZZOOOOOOOZZZZZZZZZZZZZZZZZZZI~7I,7$$$$IOZZ$ZZZ877~OIII+88?:ZZZZZZZZZ$$$$$$$$$$$$
ZZZZZZZZZZZZZZZZZZZZZZ$$$$Z$~.II$$,?$$$$DD$$$7$777ZNIIIII7D$$$$$$$$$$$$$$$$77777
$$$$ZZZZZZZZZZZZZZZZZZZZ,,,..7$II$$$,Z$$$$$DD$$77$777777N8D77$$77777777777777777
ZZZZZZZ$$$$$$$$$$$ZZZZ$$O,.OI$ZZZ88888::77$$7778DDN$I777IIO77777II7I77IIIIIIIIII
ZZZZZZZ$$$$$$$$$$$$$$$$$, II?.7Z,:8888888~:$777$$777777NDOIIIIIIII??I???????????
ZZZZZZZZZZ$$$$$$$$$7777OZ7~77$$,,:~D8MDDDDDDD~~Z7777777::IIII????????+++??++++??
ZZZZZZZ$$$ZZZZ$$$$$$$$~OOZ,$$Z:,,~:O8MNNDDDDDDDDDDDD888+?++++++++++++++++++++++?
7777$7888O$$$$$$$$$$$$,ZZ7:$$O:,,,:O~M$O8D888+DDDDDM?++++++++===++++++++++++++++
III8OZ$$$$77????????II,ZZ$:.7,,,,::,7:I?IMZD8?+++++=====888888888N88+=========++
?I8OOO$I??8:~?????????.=ZZ:,:.,,,,I=++Z?O8OO888888OO88888888888D88O88888Z=~=====
++OZZ7MN~I.I +++++++??.7I:,=,:,,,,+====Z$D8OZ88888OOOOO8888O8OOZ88OZOZZZ$OZOZ~==
+=OZ$DI+~:$,O++++++????$:,,7$8:,,,+=~.,OZOOO888D888888D8OO8888D8O8ZO8OZDOO.ZZ$OO
++$$$7??+D8+++????????8,.:,=O~::,,=~~,,~MMMMMMMMO8888DO8O8DD888D8DDDDNDD88,~88$8
++++.D8D8+????++++?88887=::=O~~:$ND88,::MMMMMMMMO8888D88888D88888D8DO88O~8+:7$7Z
+?++~8OO8+??D8$+DDDD888ID::+N8M:,,+$Z:::I7OMMMM88ZO8O8D88O888ZO8888888O:=$$?~7$7
ODND,?MMM8DNND77ODD88OO87$$ONZ,,===ZMOMNMMMMMN8MNZDMOZOONMMMMMMOOOZ8OO8:::::7OD+
OOOZZZZZZOOZOO8OOOO8OZOOOZOZD77$77$$$$ZOMMMMMMMMMMMMMMMMMMMMMMMMMMZ$8$OZO:::~:O$
88DD888O888OOOOZOZZOZZ8ZO$Z$Z$$$Z$O$Z$$ZZZOZZ$ZO$ZZDOOONZOZZZZ8ZOOOOOONZZNZO8OZO
Welcome to the SWARM Shell!
To exit use Ctrl-D.
"""

# Create our IPython config object
cfg = Config()

# If True (default), each prompt will be right-aligned with the preceding one.
cfg.PromptManager.justify = True

# Input prompt.  '\#' will be transformed to the prompt number
cfg.PromptManager.in_template = r'{color.Green}\u@\h{color.Blue}[{color.Cyan}\Y1{color.Blue}]{color.Green}|\#> '

# Continuation prompt.
cfg.PromptManager.in2_template = r'{color.Green}|{color.LightGreen}\D{color.Green}> '

# Output prompt. '\#' will be transformed to the prompt number
cfg.PromptManager.out_template = r'<\#> '

# Whether to display a banner upon starting IPython.
cfg.TerminalIPythonApp.display_banner = True

# Set the editor used by IPython (default to $EDITOR/vi/notepad).
cfg.TerminalInteractiveShell.editor = 'emacs -nw'

# The part of the banner to be printed before the profile
cfg.TerminalInteractiveShell.banner1 = swarm_shell_banner

# The part of the banner to be printed after the profile
cfg.TerminalInteractiveShell.banner2 = ''

# Set to confirm when you try to exit IPython with an EOF 
cfg.TerminalInteractiveShell.confirm_exit = False

# Setup logging
logging.basicConfig()
logging.getLogger('katcp').setLevel(logging.CRITICAL)
logging.getLogger('').setLevel(logging.INFO)

# Set up SWARM 
swarm = SwarmQuadrant(0)

# Start the IPython embedded shell
ipshell = InteractiveShellEmbed(config=cfg)
swarm_shell_magics = magics.SwarmShellMagics(ipshell, swarm)
ipshell.register_magics(swarm_shell_magics)
ipshell()
