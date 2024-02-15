#!/usr/bin/python3 -u

import os
import sys
import subprocess
import signal

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


class TestScreen:

    def __init__(self):
        self.rows = 0
        self.cols = 0
        # non-scrolling area size for top and bottom
        self.top = 3
        self.bottom = 3
        self.screen = 'main'

    def updateWindowSize(self):
        self.rows, self.cols = stty.size()
        self.setupScrollArea()

    def writeDefinedLines(self):

        for i in range(1, self.top):
            self.moveTo(0, i)
            self.writeFullRow(f'{self.screen}: Non-Scrolled Top {i}')

        for i in range(3, self.rows - 2):
            self.moveTo(0, i)
            self.writeFullRow(f'line {i}')

        for i in range(self.rows - self.bottom + 1, self.rows):
            self.moveTo(0, i)
            self.writeFullRow(f'{self.screen}: Non-Scrolled Bottom {i}')

    def _windowSizeChanged(self, sig, frame):
        self.updateWindowSize()

    def _readByte(self):
        return os.read(sys.stdin.fileno(), 1).decode()

    def _sendCSI(self, seq):
        sys.stdout.write('\033[' + seq)

    def _sendStringEscape(self, seq):
        sys.stdout.write('\033]' + seq + '\a')

    def setupScrollArea(self):
        top = self.top
        bottom = self.rows - self.bottom
        self._sendCSI(f'{top};{bottom}r')

    def moveTo(self, col, row):
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

    def switchScreen(self):
        mode = 'h' if self.screen == 'main' else 'l'
        self._sendCSI(f'?1047{mode}')
        self.setupScrollArea()
        if self.screen == 'main':
            self.screen = 'alt'
        else:
            self.screen = 'main'

    def writeFullRow(self, text):
        left = self.cols

        while left >= len(text):
            sys.stdout.write(text)
            left -= len(text)
            if left != 0:
                sys.stdout.write(' ')
                left -= 1

        sys.stdout.write(' ' * left)

    def processLongCommand(self, command):

        if '=' in command:
            key, val = command.split('=', 1)
        else:
            key = command
            val = ''

        COMMANDS = {
            'set-title': self.setTitle,
            'push-title': self.pushTitle,
            'pop-title': self.popTitle
        }

        cb = COMMANDS.get(key, None)

        if not cb:
            return

        cb(val)

    def setTitle(self, title):
        self._sendStringEscape(f'2;{title}')

    def pushTitle(self, _):
        self._sendCSI('22;2t')

    def popTitle(self, _):
        self._sendCSI('23;2t')

    def run(self):
        signal.signal(signal.SIGWINCH, self._windowSizeChanged)
        self.updateWindowSize()
        self.writeDefinedLines()
        self.moveTo(0, 10)
        stty.setEcho(False)
        stty.setLineBuffering(False)

        self.loop()

    def loop(self):
        COMMANDS = {
            'u': self.scrollUp,
            'd': self.scrollDown,
            'x': self.delLine,
            's': self.switchScreen,
            'r': self.writeDefinedLines,
            'q': sys.exit,
            'arrow-up': self.moveCursorUp,
            'arrow-down': self.moveCursorDown,
            'arrow-left': self.moveCursorLeft,
            'arrow-right': self.moveCursorRight,
        }

        # optional amount modifier supported by some commands e.g. 10d to
        # scroll 10 lines down
        self.amount = ''
        long_command = ''
        parse_long_command = False

        while True:
            sys.stdout.flush()
            ch = self._readByte()
            if ch is None:
                break
            elif parse_long_command:
                if ch == '\n':
                    self.processLongCommand(long_command)
                    long_command = ''
                    parse_long_command = False
                    continue
                else:
                    long_command += ch
                    continue

            cb = COMMANDS.get(ch, None)
            if cb:
                self.runCB(cb)
            elif ch == ':':
                parse_long_command = True
            elif ch.isnumeric():
                self.amount += ch
            elif ord(ch) == 0o33:  # ANSI escape character
                cmd = self.checkEscapeSeq()
                cb = COMMANDS.get(cmd, None)
                if cb:
                    self.runCB(cb)

    def runCB(self, cb):
        if self.amount:
            self.amount = int(self.amount)
        cb()
        self.amount = ''

    def checkEscapeSeq(self):
        nxt = self._readByte()
        if nxt == '[':
            # possibly an arrow key
            nxt = self._readByte()

            ARROWS = {
                'A': 'arrow-up',
                'B': 'arrow-down',
                'C': 'arrow-right',
                'D': 'arrow-left'
            }

            return ARROWS.get(nxt, None)


screen = TestScreen()
screen.run()
