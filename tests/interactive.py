#!/usr/bin/python3 -u

import os
import sys
import subprocess
import signal

from enum import Enum

# this script allows to test a range of escape sequences on the terminal to
# observe the behaviour, find bugs etc.
#
# it uses unbuffered input, single character commands can be used to issue
# commands. See the COMMANDS dict.


class TerminalSettings:

    def _prefix(self, on_off):
        return '' if on_off else '-'

    def _getPar(self, par, on_off):
        return '{}{}'.format(self._prefix(on_off), par)

    def _call(self, *args):
        return subprocess.check_output(('stty',) + args)

    def setEcho(self, on_off):
        self._call(self._getPar('echo', on_off))

    def setLineBuffering(self, on_off):
        self._call(self._getPar('icanon', on_off))

    def size(self):
        rows, cols = self._call('size').decode().split()
        return int(rows), int(cols)


stty = TerminalSettings()
Mode = Enum('Mode', 'INSERT NORMAL COMMAND')


class TestScreen:

    def __init__(self):
        self.rows = 0
        self.cols = 0
        # non-scrolling area size for top and bottom
        self.top = 3
        self.bottom = 3
        self.screen = 'main'
        self.mode = Mode.NORMAL
        self.running = True

    def quit(self, _):
        self.running = False

    def updateWindowSize(self):
        self.rows, self.cols = stty.size()
        self.setupScrollArea()

    def writeDefinedLines(self):

        for i in range(1, self.top):
            self.moveCursorTo(0, i)
            self.writeFullRow(f'{self.screen}: Non-Scrolled Top {i}')

        for i in range(3, self.rows - 2):
            self.moveCursorTo(0, i)
            self.writeFullRow(f'line {i}')

        for i in range(self.rows - self.bottom + 1, self.rows):
            self.moveCursorTo(0, i)
            self.writeFullRow(f'{self.screen}: Non-Scrolled Bottom {i}')

    def _windowSizeChanged(self, sig, frame):
        self.updateWindowSize()

    def _readKey(self):
        return os.read(sys.stdin.fileno(), 256).decode()

    def _sendCSI(self, seq):
        sys.stdout.write('\033[' + seq)

    def _sendStringEscape(self, seq):
        sys.stdout.write('\033]' + seq + '\a')

    def setupScrollArea(self):
        top = self.top
        bottom = self.rows - self.bottom
        self._sendCSI(f'{top};{bottom}r')

    def moveCursorTo(self, col, row):
        self._sendCSI(f'{row};{col}H')

    def scrollUp(self):
        lines = self.amount
        self._sendCSI(f'{lines}S')

    def scrollDown(self):
        lines = self.amount
        self._sendCSI(f'{lines}T')

    def delLine(self):
        lines = self.amount
        self._sendCSI(f'{lines}M')

    def moveCursorUp(self):
        lines = self.amount
        self._sendCSI(f'{lines}A')

    def moveCursorDown(self):
        lines = self.amount
        self._sendCSI(f'{lines}B')

    def moveCursorRight(self):
        cols = self.amount
        self._sendCSI(f'{cols}C')

    def moveCursorLeft(self):
        cols = self.amount
        self._sendCSI(f'{cols}D')

    def moveCursorToCol(self, col):
        self._sendCSI(f'{col}G')

    def moveCursorToBegin(self):
        self.moveCursorToCol(0)

    def moveCursorToEnd(self):
        self.moveCursorToCol(self.cols)

    def saveCursorPos(self):
        self._sendCSI('s')

    def restoreCursorPos(self):
        self._sendCSI('u')

    def switchScreen(self):
        on_off = 'on' if self.screen == 'main' else 'off'
        self.setAltScreen(on_off)
        self.setupScrollArea()
        if self.screen == 'main':
            self.screen = 'alt'
        else:
            self.screen = 'main'

    def resetTerm(self):
        subprocess.call(["reset"])

    def writeFullRow(self, text):
        left = self.cols

        while left >= len(text):
            sys.stdout.write(text)
            left -= len(text)
            if left != 0:
                sys.stdout.write(' ')
                left -= 1

        sys.stdout.write(' ' * left)

    def processCommand(self, command):

        if '=' in command:
            key, val = command.split('=', 1)
        else:
            key = command
            val = ''

        COMMANDS = {
            'set-title': self.setTitle,
            'push-title': self.pushTitle,
            'pop-title': self.popTitle,
            'insert-mode': self.setInsertMode,
            'mouse-report': self.setMouseReport,
            'altscreen': self.setAltScreen,
            'autowrap': self.setAutoWrap,
            'q': self.quit
        }

        cb = COMMANDS.get(key, None)

        if not cb:
            return "unknown command"

        return cb(val)

    def processNormal(self, cb):
        if self.amount:
            self.amount = int(self.amount)
        cb()
        if self.mode == Mode.NORMAL:
            self.enterNormalMode()

    def _checkOnOff(self, on_off):
        on_off = on_off.lower()

        if on_off in ('on', '1', 'true', 'yes'):
            return True
        elif on_off in ('off', '0', 'false', 'no'):
            return False
        else:
            return None

    def setTitle(self, title):
        self._sendStringEscape(f'2;{title}')
        return "changed title"

    def _getPrivateOnOff(self, on_off):
        on_off = self._checkOnOff(on_off)
        if on_off is None:
            return None

        return 'h' if on_off else 'l'

    def setAltScreen(self, on_off):
        mode = self._getPrivateOnOff(on_off)

        if mode:
            self._sendCSI(f'?1047{mode}')
            return f"set altscreen = {on_off}"
        else:
            return "bad value for altscreen"

    def setAutoWrap(self, on_off):
        mode = self._getPrivateOnOff(on_off)

        if mode:
            self._sendCSI(f'?7{mode}')
            return f"set autowrap = {on_off}"
        else:
            return "bad value for autowrap"

    def privateAltOn(self):
        self.privateAlt("h")

    def privateAltOff(self):
        self.privateAlt("l")

    def privateAlt(self, ch):
        self._sendCSI(f'?1049{ch}')

    def privateAlt2On(self):
        self.privateAlt2("h")

    def privateAlt2Off(self):
        self.privateAlt2("l")

    def privateAlt2(self, ch):
        self._sendCSI(f'?47{ch}')

    def setInsertMode(self, on_off):
        mode = self._getPrivateOnOff(on_off)

        if mode:
            self._sendCSI(f'4{mode}')
            return f"set insert mode = {on_off}"
        else:
            return "bad value for insert-mode"

    def setMouseReport(self, on_off):
        mode = self._getPrivateOnOff(on_off)

        if mode:
            self._sendCSI(f'?1003{mode}')
            return f"set mouse-report = {on_off}"
        else:
            return "bad value for mouse-report"

    def pushTitle(self, _):
        self._sendCSI('22;2t')
        return "pushed title"

    def popTitle(self, _):
        self._sendCSI('23;2t')
        return "popped title"

    def run(self):
        signal.signal(signal.SIGWINCH, self._windowSizeChanged)
        self.updateWindowSize()
        self.writeDefinedLines()
        self.moveCursorTo(0, 10)
        stty.setEcho(False)
        stty.setLineBuffering(False)
        self.enterNormalMode()

        try:
            self.loop()
        finally:
            self.resetTerm()

    def enterCommandMode(self):
        self.saveCursorPos()
        self.clearCommandStatus()
        self.mode = Mode.COMMAND
        # the string parsed so far
        self.cmd = ''
        self.moveCursorTo(0, self.rows)
        sys.stdout.write(':')

    def clearCommandStatus(self):
        self.moveCursorTo(0, self.rows)
        sys.stdout.write(' ' * self.cols)

    def leaveCommandMode(self, text=None):
        self.clearCommandStatus()
        if text:
            self.moveCursorTo(0, self.rows)
            sys.stdout.write(text)
        self.restoreCursorPos()
        self.mode = Mode.NORMAL

    def enterNormalMode(self):
        if self.mode == Mode.COMMAND:
            self.leaveCommandMode()

        self.mode = Mode.NORMAL
        # optional amount modifier supported by some commands e.g. 10d to
        # scroll 10 lines down
        self.amount = ''

    def enterInsertMode(self):
        self.mode = Mode.INSERT

    def loop(self):

        while self.running:
            sys.stdout.flush()
            key = self._readKey()
            if key is None:
                break

            if self.mode == Mode.NORMAL:
                self.handleNormalKey(key)
            elif self.mode == Mode.COMMAND:
                self.handleCommandKey(key)
            elif ord(key[0]) == 0o33:
                if len(key) == 1:
                    self.handleEscapeMode()
            else:  # insert mode
                sys.stdout.write(key)

    def handleEscapeMode(self):
        self.enterNormalMode()

    def handleNormalKey(self, key):
        COMMANDS = {
            'u': self.scrollUp,
            'd': self.scrollDown,
            'x': self.delLine,
            's': self.switchScreen,
            'r': self.writeDefinedLines,
            'q': sys.exit,
            'i': self.enterInsertMode,
            ':': self.enterCommandMode,
            'h': self.moveCursorLeft,
            'j': self.moveCursorDown,
            'k': self.moveCursorUp,
            'l': self.moveCursorRight,
            '$': self.moveCursorToEnd,
            '^': self.moveCursorToBegin,
            'S': self.saveCursorPos,
            'R': self.restoreCursorPos,
            'Z': self.privateAltOn,
            'z': self.privateAltOff,
            'A': self.privateAlt2On,
            'a': self.privateAlt2Off,
            'arrow-up': self.moveCursorUp,
            'arrow-down': self.moveCursorDown,
            'arrow-left': self.moveCursorLeft,
            'arrow-right': self.moveCursorRight,
        }

        cb = COMMANDS.get(key, None)
        if cb:
            self.processNormal(cb)
        elif key.isnumeric():
            self.amount += key
        elif ord(key[0]) == 0o33:  # ANSI escape character
            if len(key) == 1:
                self.enterNormalMode()
            else:
                cmd = self.checkEscapeSeq(key)
                cb = COMMANDS.get(cmd, None)
                if cb:
                    self.processNormal(cb)

    def handleCommandKey(self, key):
        if key == '\n':
            reply = self.processCommand(self.cmd)
            self.leaveCommandMode(reply)
            self.cmd = ''
            self.enterNormalMode()
        elif key[0] == 0o33:
            if len(key) == 1:
                self.enterNormalMode()
        elif len(key) == 1 and ord(key[0]) == 0x7F:
            self.cmd = self.cmd[:-1]
            self.moveCursorLeft()
            sys.stdout.write(' ')
            self.moveCursorLeft()
        else:
            self.cmd += key
            sys.stdout.write(key)

    def checkEscapeSeq(self, seq):
        if seq[1] == '[' and len(seq) > 2:
            # possibly an arrow key

            ARROWS = {
                'A': 'arrow-up',
                'B': 'arrow-down',
                'C': 'arrow-right',
                'D': 'arrow-left'
            }

            return ARROWS.get(seq[2], None)


screen = TestScreen()
screen.run()
