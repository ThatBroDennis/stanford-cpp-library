/*
 * File: gconsolewindow.cpp
 * ------------------------
 * This file implements the gconsolewindow.h interface.
 *
 * @author Marty Stepp
 * @version 2018/09/27
 * - bug fix for printing strings with line breaks (remove \r, favor \n)
 * @version 2018/09/23
 * - added getFont
 * - bug fix for loading input scripts
 * - bug fix for default font on Mac
 * @version 2018/09/18
 * - window size/location fixes
 * @version 2018/09/17
 * - fixes for monospaced font on Mac OS X
 * @version 2018/08/23
 * - initial version, separated out from console.cpp
 */

#define INTERNAL_INCLUDE 1
#include "gconsolewindow.h"
#include <cstdio>
#include <QAction>
#include <QTextDocumentFragment>
#include "error.h"
#include "exceptions.h"
#include "filelib.h"
#include "gclipboard.h"
#include "gcolor.h"
#include "gcolorchooser.h"
#include "gdiffgui.h"
#include "gdownloader.h"
#include "gfilechooser.h"
#include "gfont.h"
#include "gfontchooser.h"
#include "goptionpane.h"
#include "gthread.h"
#include "os.h"
#include "qtgui.h"
#include "private/static.h"
#include "private/version.h"
#undef INTERNAL_INCLUDE

void setConsolePropertiesQt();

const bool GConsoleWindow::GConsoleWindow::ALLOW_RICH_INPUT_EDITING = true;
const double GConsoleWindow::DEFAULT_WIDTH = 800;
const double GConsoleWindow::DEFAULT_HEIGHT = 500;
const double GConsoleWindow::DEFAULT_X = 10;
const double GConsoleWindow::DEFAULT_Y = 40;
const std::string GConsoleWindow::CONFIG_FILE_NAME = "spl-jar-settings.txt";
const std::string GConsoleWindow::DEFAULT_WINDOW_TITLE = "Console";
const std::string GConsoleWindow::DEFAULT_FONT_FAMILY = "Monospace";
const std::string GConsoleWindow::DEFAULT_FONT_WEIGHT = "";
const int GConsoleWindow::DEFAULT_FONT_SIZE = 12;
const int GConsoleWindow::MIN_FONT_SIZE = 4;
const int GConsoleWindow::MAX_FONT_SIZE = 255;
const std::string GConsoleWindow::DEFAULT_BACKGROUND_COLOR = "white";
const std::string GConsoleWindow::DEFAULT_ERROR_COLOR = "red";
const std::string GConsoleWindow::DEFAULT_OUTPUT_COLOR = "black";
const std::string GConsoleWindow::USER_INPUT_COLOR = "blue";
GConsoleWindow* GConsoleWindow::_instance = nullptr;
bool GConsoleWindow::_consoleEnabled = false;

/* static */ bool GConsoleWindow::consoleEnabled() {
    return _consoleEnabled;
}

/* static */ std::string GConsoleWindow::getDefaultFont() {
    if (OS::isMac()) {
        // for some reason, using "Monospace" doesn't work for me on Mac testing
        return "Menlo-"
                + integerToString(DEFAULT_FONT_SIZE + 1)
                + (DEFAULT_FONT_WEIGHT.empty() ? "" : ("-" + DEFAULT_FONT_WEIGHT));
    } else {
        return DEFAULT_FONT_FAMILY
                + "-" + integerToString(DEFAULT_FONT_SIZE)
                + (DEFAULT_FONT_WEIGHT.empty() ? "" : ("-" + DEFAULT_FONT_WEIGHT));
    }
}

/* static */ GConsoleWindow* GConsoleWindow::instance() {
    if (!_instance) {
        // initialize Qt system and Qt Console window
        GThread::runOnQtGuiThread([]() {
            QtGui::instance()->initializeQt();
            _instance = new GConsoleWindow();
            setConsolePropertiesQt();
        });
    }
    return _instance;
}

/* static */ void GConsoleWindow::setConsoleEnabled(bool enabled) {
    _consoleEnabled = enabled;
}

GConsoleWindow::GConsoleWindow()
        : GWindow(/* visible */ false),
          _textArea(nullptr),
          _clearEnabled(true),
          _echo(false),
          _locationSaved(false),
          _locked(false),
          _promptActive(false),
          _shutdown(false),
          _commandHistoryIndex(-1),
          _errorColor(""),
          _outputColor(""),
          _inputBuffer(""),
          _lastSaveFileName(""),
          _cinout_new_buf(nullptr),
          _cerr_new_buf(nullptr) {
    _initMenuBar();
    _initWidgets();
    _initStreams();
    loadConfiguration();
}

void GConsoleWindow::_initMenuBar() {
    const std::string ICON_FOLDER = "icons/";

    // File menu
    addMenu("&File");
    addMenuItem("File", "&Save", ICON_FOLDER + "save.gif",
                [this]() { this->save(); })
                ->setShortcut(QKeySequence::Save);

    addMenuItem("File", "Save &As...", ICON_FOLDER + "save_as.gif",
                [this]() { this->saveAs(); })
                ->setShortcut(QKeySequence::SaveAs);
    addMenuSeparator("File");

    addMenuItem("File", "&Print", ICON_FOLDER + "print.gif",
                [this]() { this->showPrintDialog(); })
                ->setShortcut(QKeySequence::Print);
    setMenuItemEnabled("File", "Print", false);
    addMenuSeparator("File");

    addMenuItem("File", "&Load Input Script...", ICON_FOLDER + "script.gif",
                [this]() { this->showInputScriptDialog(); });

    addMenuItem("File", "&Compare Output...", ICON_FOLDER + "compare_output.gif",
                [this]() { this->showCompareOutputDialog(); });

    addMenuItem("File", "&Quit", ICON_FOLDER + "quit.gif",
                [this]() { this->close(); /* TODO: exit app */ })
                ->setShortcut(QKeySequence::Quit);

    // Edit menu
    addMenu("&Edit");
    addMenuItem("Edit", "Cu&t", ICON_FOLDER + "cut.gif",
                [this]() { this->clipboardCut(); })
                ->setShortcut(QKeySequence::Cut);

    addMenuItem("Edit", "&Copy", ICON_FOLDER + "copy.gif",
                [this]() { this->clipboardCopy(); })
                ->setShortcut(QKeySequence::Copy);

    addMenuItem("Edit", "&Paste", ICON_FOLDER + "paste.gif",
                [this]() { this->clipboardPaste(); })
                ->setShortcut(QKeySequence::Paste);

    addMenuItem("Edit", "Select &All", ICON_FOLDER + "select_all.gif",
                [this]() { this->selectAll(); })
                ->setShortcut(QKeySequence::SelectAll);

    addMenuItem("Edit", "C&lear Console", ICON_FOLDER + "clear_console.gif",
                [this]() { this->clearConsole(); })
                ->setShortcut(QKeySequence(QString::fromStdString("Ctrl+L")));

    // Options menu
    addMenu("&Options");
    addMenuItem("Options", "&Font...", ICON_FOLDER + "font.gif",
                [this]() { this->showFontDialog(); });

    addMenuItem("Options", "&Background Color...", ICON_FOLDER + "background_color.gif",
                [this]() { this->showColorDialog(/* background */ true); });

    addMenuItem("Options", "&Text Color...", ICON_FOLDER + "text_color.gif",
                [this]() { this->showColorDialog(/* background */ false); });

    // Help menu
    addMenu("&Help");
    addMenuItem("Help", "&About...", ICON_FOLDER + "about.gif",
                [this]() { this->showAboutDialog(); })
                ->setShortcut(QKeySequence::HelpContents);

    addMenuItem("Help", "&Check for Updates", ICON_FOLDER + "check_for_updates.gif",
                [this]() { this->checkForUpdates(); });
}

void GConsoleWindow::_initStreams() {
    // buffer C-style stderr
    static char stderrBuf[BUFSIZ + 10] = {'\0'};
    std::ios::sync_with_stdio(false);
    setbuf(stderr, stderrBuf);

    // redirect cin/cout/cerr
    _cinout_new_buf = new stanfordcpplib::qtgui::ConsoleStreambufQt();
    _cerr_new_buf = new stanfordcpplib::qtgui::ConsoleStreambufQt(/* isStderr */ true);
    std::cin.rdbuf(_cinout_new_buf);
    std::cout.rdbuf(_cinout_new_buf);
    std::cerr.rdbuf(_cerr_new_buf);
}

void GConsoleWindow::_initWidgets() {
    _textArea = new GTextArea();
    _textArea->setColor("black");
    _textArea->setContextMenuEnabled(false);
    // _textArea->setEditable(false);
    _textArea->setLineWrap(false);
    _textArea->setFont(getDefaultFont());
    // _textArea->setRowsColumns(25, 70);
    QTextEdit* rawTextEdit = static_cast<QTextEdit*>(_textArea->getWidget());
    rawTextEdit->setTabChangesFocus(false);
    _textArea->setKeyListener([this](GEvent event) {
        if (event.getEventType() == KEY_PRESSED) {
            this->processKeyPress(event);
        } else if (event.getEventType() == KEY_RELEASED
                   || event.getEventType() == KEY_TYPED) {
            event.ignore();
        }
    });
    _textArea->setMouseListener([](GEvent event) {
        // snuff out mouse-based operations:
        // - popping up context menu by right-clicking
        // - Linux-style copy/paste operations using selection plus middle-click
        if (event.getButton() > 1
                || event.getEventType() == MOUSE_RELEASED) {
            event.ignore();
        }
    });
    addToRegion(_textArea, "Center");

    setTitle(DEFAULT_WINDOW_TITLE);
    setCloseOperation(GWindow::CLOSE_HIDE);
    setLocation(DEFAULT_X, DEFAULT_Y);
    setSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    setVisible(true);
}


GConsoleWindow::~GConsoleWindow() {
    // empty
}

void GConsoleWindow::checkForUpdates() {
    GThread::runInNewThreadAsync([this]() {
        static const std::string CPP_ZIP_VERSION_URL = version::getCppLibraryDocsUrl() + "CURRENTVERSION_CPPLIB.txt";
        std::string currentVersion = version::getCppLibraryVersion();

        GDownloader downloader;
        std::string latestVersion = trim(downloader.downloadAsString(CPP_ZIP_VERSION_URL));

        if (latestVersion.empty()) {
            GOptionPane::showMessageDialog(
                    /* parent  */ getWidget(),
                    /* message */ "Unable to look up latest library version from web.",
                    /* title   */ "Network error",
                    /* type    */ GOptionPane::MESSAGE_ERROR);
            return;
        }

        std::string message;
        if (currentVersion >= latestVersion) {
                message = "This project already has the latest version \nof the Stanford libraries (" + currentVersion + ").";
        } else {
                message = "<html>There is an updated version of the Stanford libraries available.\n\n"
                   "This project's library version: " + currentVersion + "\n"
                   "Current newest library version: " + latestVersion + "\n\n"
                   "Go to <a href=\"" + version::getCppLibraryDocsUrl() + "\">"
                   + version::getCppLibraryDocsUrl() + "</a> to get the new version.</html>";
        }
        GOptionPane::showMessageDialog(
                    /* parent  */ getWidget(),
                    /* message */ message);
    });
}

void GConsoleWindow::clearConsole() {
    if (_shutdown) {
        return;
    }
    std::string msg = "==================== (console cleared) ====================";
    if (_clearEnabled) {
        // print to standard console (not Stanford graphical console)
        printf("%s\n", msg.c_str());

        // clear the graphical console window
        _coutMutex.lock();
        _textArea->clearText();
        _coutMutex.unlock();
    } else {
        // don't actually clear the window, just display 'cleared' message on it
        println(msg);
    }
}

void GConsoleWindow::clipboardCopy() {
    std::string selectedText = _textArea->getSelectedText();
    if (!selectedText.empty()) {
        GClipboard::set(selectedText);
    }
}

void GConsoleWindow::clipboardCut() {
    if (_shutdown || !_promptActive || !ALLOW_RICH_INPUT_EDITING) {
        return;
    }

    // if selection is entirely within the user input area, cut out of user input area
    int userInputStart = getUserInputStart();
    int userInputEnd   = getUserInputEnd();
    int selectionStart = _textArea->getSelectionStart();
    int selectionEnd = _textArea->getSelectionEnd();
    if (selectionEnd > selectionStart
            && selectionStart >= userInputStart
            && selectionEnd <= userInputEnd) {
        // selection is entirely user input! cut it!
        QTextFragment frag = getUserInputFragment();
        if (frag.isValid()) {
            std::string selectedText = _textArea->getSelectedText();
            QTextEdit* textArea = static_cast<QTextEdit*>(this->_textArea->getWidget());
            QTextCursor cursor(textArea->textCursor());

            int indexStart = selectionStart - userInputStart;
            int selectionLength = _textArea->getSelectionLength();
            _cinMutex.lockForWrite();
            _inputBuffer.erase(indexStart, selectionLength);
            cursor.beginEditBlock();
            cursor.removeSelectedText();
            cursor.endEditBlock();
            textArea->setTextCursor(cursor);
            _cinMutex.unlock();
            GClipboard::set(selectedText);
        }
    }
}

void GConsoleWindow::clipboardPaste() {
    if (_shutdown) {
        return;
    }

    _textArea->clearSelection();
    if (!isCursorInUserInputArea()) {
        _textArea->moveCursorToEnd();
    }

    std::string clipboardText = GClipboard::get();
    for (int i = 0; i < (int) clipboardText.length(); i++) {
        if (clipboardText[i] == '\r') {
            continue;
        } else if (clipboardText[i] == '\n') {
            processUserInputEnterKey();
        } else {
            processUserInputKey(clipboardText[i]);
        }
    }
}

void GConsoleWindow::close() {
    shutdown();
    GWindow::close();   // call super
}

void GConsoleWindow::compareOutput(const std::string& filename) {
    std::string expectedOutput;
    if (!filename.empty() && fileExists(filename)) {
        expectedOutput = readEntireFile(filename);
    } else {
        expectedOutput = "File not found: " + filename;
    }

    std::string studentOutput = getAllOutput();

    GDiffGui::showDialog("expected output", expectedOutput,
                         "your output", studentOutput,
                         /* showCheckBoxes */ false);
}

std::string GConsoleWindow::getAllOutput() const {
    GConsoleWindow* thisHack = const_cast<GConsoleWindow*>(this);
    thisHack->_coutMutex.lock();
    std::string allOutput = thisHack->_allOutputBuffer.str();
    thisHack->_coutMutex.unlock();
    return allOutput;
}

std::string GConsoleWindow::getBackground() const {
    return _textArea->getBackground();
}

int GConsoleWindow::getBackgroundInt() const {
    return _textArea->getBackgroundInt();
}

std::string GConsoleWindow::getColor() const {
    return getOutputColor();
}

int GConsoleWindow::getColorInt() const {
    return GColor::convertColorToRGB(getOutputColor());
}

std::string GConsoleWindow::getErrorColor() const {
    return _errorColor.empty() ? DEFAULT_ERROR_COLOR : _errorColor;
}

std::string GConsoleWindow::getFont() const {
    return _textArea->getFont();
}

std::string GConsoleWindow::getForeground() const {
    return getOutputColor();
}

int GConsoleWindow::getForegroundInt() const {
    return GColor::convertColorToRGB(getOutputColor());
}

std::string GConsoleWindow::getOutputColor() const {
    return _outputColor.empty() ? DEFAULT_OUTPUT_COLOR : _outputColor;
}

QTextFragment GConsoleWindow::getUserInputFragment() const {
    if (!_inputBuffer.empty()) {
        QTextEdit* textArea = static_cast<QTextEdit*>(this->_textArea->getWidget());
        QTextBlock block = textArea->document()->end().previous();
        while (block.isValid()) {
            QTextBlock::iterator it;
            for (it = block.begin(); !(it.atEnd()); ++it) {
                QTextFragment frag = it.fragment();
                if (frag.isValid()) {
                    std::string fragText = frag.text().toStdString();

                    // see if it is the given user input
                    if (fragText == _inputBuffer) {
                        return frag;
                    }
                }
            }
            block = block.previous();
        }
    }

    // didn't find the fragment; this will return an 'invalid' fragment
    QTextFragment notFound;
    return notFound;
}

int GConsoleWindow::getUserInputStart() const {
    QTextFragment frag = getUserInputFragment();
    if (frag.isValid()) {
        return frag.position();
    } else if (_promptActive) {
        // at end of text
        return (int) _textArea->getText().length();
    } else {
        return -1;
    }
}

int GConsoleWindow::getUserInputEnd() const {
    QTextFragment frag = getUserInputFragment();
    if (frag.isValid()) {
        return frag.position() + frag.length();
    } else if (_promptActive) {
        // at end of text
        return (int) _textArea->getText().length();
    } else {
        return -1;
    }
}

bool GConsoleWindow::isClearEnabled() const {
    return _clearEnabled;
}

bool GConsoleWindow::isCursorInUserInputArea() const {
    int cursorPosition = _textArea->getCursorPosition();
    int userInputStart = getUserInputStart();
    int userInputEnd   = getUserInputEnd();
    return _promptActive
            && userInputStart <= cursorPosition
            && cursorPosition <= userInputEnd;
}

bool GConsoleWindow::isEcho() const {
    return _echo;
}

bool GConsoleWindow::isLocationSaved() const {
    return _locationSaved;
}

bool GConsoleWindow::isLocked() const {
    return _locked;
}

bool GConsoleWindow::isSelectionInUserInputArea() const {
    int userInputStart = getUserInputStart();
    int userInputEnd   = getUserInputEnd();
    int selectionStart = _textArea->getSelectionStart();
    int selectionEnd = _textArea->getSelectionEnd();
    return userInputStart >= 0 && userInputEnd >= 0
            && selectionStart >= userInputStart
            && selectionEnd <= userInputEnd;
}

void GConsoleWindow::loadConfiguration() {
    std::string configFile = getTempDirectory() + "/" + CONFIG_FILE_NAME;
    if (fileExists(configFile)) {
        std::ifstream infile;
        infile.open(configFile.c_str());
        if (!infile) {
                return;
        }
        std::string line;
        while (getline(infile, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }
            Vector<std::string> tokens = stringSplit(line, "=");
            if (tokens.size() < 2) {
                continue;
            }
            std::string key   = toLowerCase(tokens[0]);
            std::string value = tokens[1];
            if (key == "font") {
                setFont(value);
            } else if (key == "background") {
                setBackground(value);
            } else if (key == "foreground") {
                setForeground(value);
            }
        }
    }
}

void GConsoleWindow::loadInputScript(int number) {
    std::string sep = getDirectoryPathSeparator();
    static std::initializer_list<std::string> directoriesToCheck {
            ".",
            "." + sep + "input",
            "." + sep + "output"
    };
    std::string inputFile;
    std::string expectedOutputFile;
    for (std::string dir : directoriesToCheck) {
        if (!isDirectory(dir)) {
            continue;
        }

        for (std::string filename : listDirectory(dir)) {
            filename = dir + sep + filename;
            if (inputFile.empty()
                    && stringContains(filename, "input-" + integerToString(number))
                    && endsWith(filename, ".txt")) {
                inputFile = filename;
            } else if (expectedOutputFile.empty()
                       && stringContains(filename, "expected-output-" + integerToString(number))
                       && endsWith(filename, ".txt")) {
                expectedOutputFile = filename;
            }
        }
    }

    if (!inputFile.empty()) {
        loadInputScript(inputFile);
        pause(500);
    }
    if (!expectedOutputFile.empty()) {
        GThread::runInNewThreadAsync([this, expectedOutputFile]() {
            pause(500);
            compareOutput(expectedOutputFile);
        });
    }
}

void GConsoleWindow::loadInputScript(const std::string& filename) {
    if (!filename.empty() && fileExists(filename)) {
        std::ifstream infile;
        infile.open(filename.c_str());
        Vector<std::string> lines;
        readEntireFile(infile, lines);

        _cinQueueMutex.lockForWrite();
        _inputScript.clear();
        for (std::string line : lines) {
            _inputScript.enqueue(line);
        }
        _cinQueueMutex.unlock();
    }
}

void GConsoleWindow::print(const std::string& str, bool isStdErr) {
    if (_shutdown) {
        return;
    }
    if (_echo) {
        fflush(isStdErr ? stdout : stderr);
        fflush(isStdErr ? stderr : stdout);
        fprintf(isStdErr ? stderr : stdout, "%s", str.c_str());
        if (str.find("\n") != std::string::npos) {
            fflush(isStdErr ? stderr : stdout);
            fflush(isStdErr ? stdout : stderr);
        }
    }

    // clean up line breaks (remove \r)
    std::string strToPrint = str;
    stringReplaceInPlace(strToPrint, "\r\n", "\n");
    stringReplaceInPlace(strToPrint, "\r", "\n");

    GThread::runOnQtGuiThreadAsync([this, strToPrint, isStdErr]() {
        _coutMutex.lock();
        _allOutputBuffer << strToPrint;
        this->_textArea->appendFormattedText(strToPrint, isStdErr ? getErrorColor() : getOutputColor());
        this->_textArea->moveCursorToEnd();
        this->_textArea->scrollToBottom();
        _coutMutex.unlock();
    });
}

void GConsoleWindow::println(bool isStdErr) {
    print("\n", isStdErr);
}

void GConsoleWindow::println(const std::string& str, bool isStdErr) {
    print(str + "\n", isStdErr);
}

void GConsoleWindow::processKeyPress(GEvent event) {
    // TODO: should this be done in a different thread?
    char key = event.getKeyChar();
    int keyCode = event.getKeyCode();

    if (event.isCtrlOrCommandKeyDown()) {
        if (keyCode == Qt::Key_Plus || keyCode == Qt::Key_Equal) {
            // increase font size
            event.ignore();
            QFont font = GFont::toQFont(_textArea->getFont());
            if (font.pointSize() + 1 <= MAX_FONT_SIZE) {
                font.setPointSize(font.pointSize() + 1);
                setFont(GFont::toFontString(font));
            }
        } else if (keyCode == Qt::Key_Minus) {
            // decrease font size
            event.ignore();
            QFont font = GFont::toQFont(_textArea->getFont());
            if (font.pointSize() - 1 >= MIN_FONT_SIZE) {
                font.setPointSize(font.pointSize() - 1);
                setFont(GFont::toFontString(font));
            }
        } else if (keyCode == Qt::Key_Insert) {
            // Ctrl+Ins => Copy
            event.ignore();
            clipboardCopy();
        } else if (keyCode == Qt::Key_0) {
            // normalize font size
            event.ignore();
            setFont(DEFAULT_FONT_FAMILY + "-" + integerToString(DEFAULT_FONT_SIZE));
        } else if (keyCode >= Qt::Key_1 && keyCode <= Qt::Key_9) {
            // load input script 1-9
            loadInputScript(keyCode - Qt::Key_0);
        } else if (keyCode == Qt::Key_C) {
            event.ignore();
            clipboardCopy();
        } else if (event.isCtrlKeyDown() && keyCode == Qt::Key_D) {
            event.ignore();
            processEof();
        } else if (keyCode == Qt::Key_L) {
            event.ignore();
            clearConsole();
        } else if (keyCode == Qt::Key_Q || keyCode == Qt::Key_W) {
            event.ignore();
            close();
        } else if (keyCode == Qt::Key_S) {
            event.ignore();
            if (event.isShiftKeyDown()) {
                saveAs();
            } else {
                save();
            }
        } else if (keyCode == Qt::Key_V) {
            event.ignore();
            clipboardPaste();
        } else if (keyCode == Qt::Key_X) {
            event.ignore();
            clipboardCut();
        }
    }

    if (_shutdown) {
        return;
    }

    if (event.isCtrlOrCommandKeyDown() || event.isAltKeyDown()) {
        // system hotkey; let the normal keyboard handler process this event
        event.ignore();
        return;
    }

    switch (keyCode) {
        case GEvent::PAGE_UP_KEY:
        case GEvent::PAGE_DOWN_KEY:
            // don't ignore event
            break;
        case GEvent::BACKSPACE_KEY: {
            event.ignore();
            processBackspace(keyCode);
            break;
        }
        case GEvent::DELETE_KEY: {
            event.ignore();
            if (event.isShiftKeyDown()) {
                clipboardCut();   // Shift+Del => Cut
            } else {
                processBackspace(keyCode);
            }
            break;
        }
        case GEvent::INSERT_KEY: {
            event.ignore();
            if (event.isShiftKeyDown()) {
                clipboardPaste();   // Shift+Ins => Paste
            }
            break;
        }
        case GEvent::HOME_KEY:
            if (ALLOW_RICH_INPUT_EDITING) {
                // move to start of input buffer
                if (_promptActive) {
                    event.ignore();
                    int start = getUserInputStart();
                    if (start >= 0) {
                        _textArea->setCursorPosition(
                                start,
                                /* keepAnchor */ event.isShiftKeyDown() && isCursorInUserInputArea());
                    } else {
                        _textArea->moveCursorToEnd();
                    }
                }
            } else {
                event.ignore();
            }
            break;
        case GEvent::END_KEY:
            if (ALLOW_RICH_INPUT_EDITING) {
                // move to end of input buffer
                if (_promptActive) {
                    event.ignore();
                    int end = getUserInputEnd();
                    if (end >= 0) {
                        _textArea->setCursorPosition(
                                end,
                                /* keepAnchor */ event.isShiftKeyDown() && isCursorInUserInputArea());
                    } else {
                        _textArea->moveCursorToEnd();
                    }
                }
            } else {
                event.ignore();
            }
            break;
        case GEvent::LEFT_ARROW_KEY: {
            // bound within user input area if a prompt is active
            if (ALLOW_RICH_INPUT_EDITING) {
                if (isCursorInUserInputArea()) {
                    int cursorPosition = _textArea->getCursorPosition();
                    int userInputStart = getUserInputStart();
                    if (cursorPosition <= userInputStart) {
                        event.ignore();
                        if (!event.isShiftKeyDown()) {
                            _textArea->clearSelection();
                        }
                    }
                }
            } else {
                event.ignore();
            }
            break;
        }
        case GEvent::RIGHT_ARROW_KEY:
            // bound within user input area if a prompt is active
            if (ALLOW_RICH_INPUT_EDITING) {
                if (isCursorInUserInputArea()) {
                    int cursorPosition = _textArea->getCursorPosition();
                    int userInputEnd   = getUserInputEnd();
                    if (cursorPosition >= userInputEnd) {
                        event.ignore();
                        if (!event.isShiftKeyDown()) {
                            _textArea->clearSelection();
                        }
                    }
                }
            } else {
                event.ignore();
            }
            break;
        case GEvent::UP_ARROW_KEY:
            if (isCursorInUserInputArea()) {
                event.ignore();
                processCommandHistory(/* delta */ -1);
            }
            break;
        case GEvent::DOWN_ARROW_KEY:
            if (isCursorInUserInputArea()) {
                event.ignore();
                processCommandHistory(/* delta */ 1);
            }
            break;
        case GEvent::TAB_KEY:
            // TODO: tab completion?
        case GEvent::CLEAR_KEY:
            break;
        case GEvent::F1_KEY: {
            event.ignore();
            showAboutDialog();
            break;
        }
        case GEvent::F2_KEY:
        case GEvent::F3_KEY:
        case GEvent::F4_KEY:
        case GEvent::F5_KEY:
        case GEvent::F6_KEY:
        case GEvent::F7_KEY:
        case GEvent::F8_KEY:
        case GEvent::F9_KEY:
        case GEvent::F10_KEY:
        case GEvent::F11_KEY:
        case GEvent::F12_KEY:
        case GEvent::HELP_KEY: {
            // various control/modifier keys: do nothing / consume event
            event.ignore();
            break;
        }
        case GEvent::SHIFT_KEY:
        case GEvent::CTRL_KEY:
        case GEvent::ALT_KEY:
        case GEvent::PAUSE_KEY:
        case GEvent::CAPS_LOCK_KEY:
        case GEvent::ESCAPE_KEY:
        case GEvent::NUM_LOCK_KEY:
        case GEvent::SCROLL_LOCK_KEY:
        case GEvent::PRINT_SCREEN_KEY:
        case GEvent::META_KEY:
        case GEvent::WINDOWS_KEY:
        case GEvent::MENU_KEY: {
            // various other control/modifier keys: let OS have the event (don't call ignore())
            break;
        }
        case GEvent::RETURN_KEY:
        case GEvent::ENTER_KEY: {
            // \n end line
            event.ignore();
            processUserInputEnterKey();
            break;
        }
        default: {
            event.ignore();
            processUserInputKey(key);
            break;
        }
    }
}

void GConsoleWindow::processBackspace(int key) {
    if (_shutdown || !_promptActive) {
        return;
    }

    // check whether it is a backspace or a delete
    bool isBackspace = key == GEvent::BACKSPACE_KEY /* TODO: or computer is Mac */;

    _cinMutex.lockForWrite();
    if (!_inputBuffer.empty()) {
        // remove last char from onscreen text editor:
        // - find last blue area
        QTextFragment frag = getUserInputFragment();
        if (frag.isValid()) {
            // remove last char from onscreen document fragment
            QTextEdit* textArea = static_cast<QTextEdit*>(this->_textArea->getWidget());
            QTextCursor cursor(textArea->textCursor());

            int oldCursorPosition = cursor.position();
            int indexToDelete = (int) _inputBuffer.length() - 1;
            int userInputIndexMin = frag.position();
            int userInputIndexMax = frag.position() + frag.length() - (isBackspace ? 0 : 1);

            if (oldCursorPosition >= userInputIndexMin && oldCursorPosition < userInputIndexMax) {
                // cursor is inside the user input fragment;
                // figure out which character it's on so we can delete it
                indexToDelete = oldCursorPosition - frag.position() - (isBackspace ? 1 : 0);
            } else {
                // cursor is outside of the user input fragment; move it there
                cursor.setPosition(frag.position() + frag.length());
            }

            if (indexToDelete >= 0 && indexToDelete < (int) _inputBuffer.length()) {
                if (isBackspace || indexToDelete == (int) _inputBuffer.length() - 1) {
                    cursor.deletePreviousChar();
                } else {
                    cursor.deleteChar();   // Delete
                }

                // remove last char from internal input buffer
                _inputBuffer.erase(indexToDelete, 1);
            }
        }
    }
    _cinMutex.unlock();
}

void GConsoleWindow::processCommandHistory(int delta) {
    _cinMutex.lockForRead();
    std::string oldCommand = "";
    _commandHistoryIndex += delta;
    _commandHistoryIndex = std::max(-1, _commandHistoryIndex);
    _commandHistoryIndex = std::min(_commandHistoryIndex, _inputCommandHistory.size());
    if (0 <= _commandHistoryIndex && _commandHistoryIndex < _inputCommandHistory.size()) {
        oldCommand = _inputCommandHistory[_commandHistoryIndex];
    }
    _cinMutex.unlock();
    setUserInput(oldCommand);
}

void GConsoleWindow::processEof() {
    // only set EOF if input buffer is empty; this is the behavior on most *nix consoles
    if (_inputBuffer.empty()) {
        std::cin.setstate(std::ios_base::eofbit);
    }
}

void GConsoleWindow::processUserInputEnterKey() {
    if (_shutdown) {
        return;
    }
    _cinMutex.lockForWrite();
    _cinQueueMutex.lockForWrite();
    _inputLines.enqueue(_inputBuffer);
    _inputCommandHistory.add(_inputBuffer);
    _commandHistoryIndex = _inputCommandHistory.size();
    _cinQueueMutex.unlock();
    _allOutputBuffer << _inputBuffer << std::endl;
    _inputBuffer = "";   // clear input buffer
    this->_textArea->appendFormattedText("\n", USER_INPUT_COLOR);
    _cinMutex.unlock();
}

void GConsoleWindow::processUserInputKey(int key) {
    if (_shutdown) {
        return;
    }
    if (key != '\0' && isprint(key)) {
        // normal key: append to user input buffer
        _cinMutex.lockForWrite();

        std::string keyStr = charToString((char) key);

        bool inserted = false;
        if (ALLOW_RICH_INPUT_EDITING && isCursorInUserInputArea()) {
            QTextFragment frag = getUserInputFragment();
            if (frag.isValid()) {
                QTextEdit* textArea = static_cast<QTextEdit*>(this->_textArea->getWidget());
                QTextCursor cursor(textArea->textCursor());

                // BUGFIX: if there is any selected text, remove it first
                int fragStart = frag.position();
                int selectionStart = cursor.selectionStart() - fragStart;
                int selectionEnd = cursor.selectionEnd() - fragStart;
                if (selectionEnd > selectionStart
                        && selectionStart >= 0
                        && selectionEnd <= (int) _inputBuffer.length()) {
                    cursor.removeSelectedText();
                    _inputBuffer.erase(selectionStart, selectionEnd - selectionStart);
                }

                int cursorPosition = cursor.position();
                int indexToInsert = cursorPosition - frag.position();
                if (indexToInsert == 0) {
                    // special case for inserting at start of fragment.
                    // example: fragment is "abcde", cursor at start, user types "x".
                    // if we just insert the "x" in the document, it won't be part of
                    // the same fragment and won't have the blue bold format.
                    // So what we do is temporarily insert it after the first character,
                    // then delete the first character, so that everything is inside
                    // the formatted span.
                    // "abcde"
                    //  ^
                    //   ^          move right by 1
                    // "axabcde"    insert "xa" at index 1
                    //     ^
                    //   ^          move left by 2
                    // "xabcde"     delete previous character "a" from index 0
                    //  ^
                    //   ^          move right by 1
                    cursor.beginEditBlock();

                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);             // move to index 1
                    cursor.insertText(QString::fromStdString(keyStr + _inputBuffer.substr(0, 1)));   // insert new char + old first char
                    cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 2);              // delete old copy of first char
                    cursor.deletePreviousChar();
                    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 1);             // move to index 1
                    cursor.endEditBlock();
                    textArea->setTextCursor(cursor);
                } else {
                    cursor.beginEditBlock();
                    cursor.insertText(QString::fromStdString(keyStr));
                    cursor.endEditBlock();
                    textArea->setTextCursor(cursor);
                }
                _inputBuffer.insert(indexToInsert, keyStr);
                inserted = true;
            }
        }

        if (!inserted) {
            // append to end of buffer/fragment
            _inputBuffer += keyStr;
            // display in blue highlighted text
            this->_textArea->appendFormattedText(keyStr, USER_INPUT_COLOR, "*-*-Bold");
        }

        _cinMutex.unlock();
    }
}

std::string GConsoleWindow::readLine() {
    // TODO: threads/locking
    // wait for a line to be available in queue
    std::string line;
    if (_shutdown) {
        return line;
    }

    this->_textArea->moveCursorToEnd();
    this->_textArea->scrollToBottom();
    this->toFront();   // move window to front on prompt for input
    this->_textArea->requestFocus();

    _cinMutex.lockForWrite();
    _promptActive = true;
    _cinMutex.unlock();

    while (!_shutdown && !std::cin.eof()) {
        bool lineRead = false;
        if (!_inputScript.isEmpty()) {
            _cinQueueMutex.lockForWrite();
            line = _inputScript.dequeue();
            lineRead = true;
            _cinQueueMutex.unlock();

            // echo user input, as if the user had just typed it
            GThread::runOnQtGuiThreadAsync([this, line]() {
                _coutMutex.lock();
                _allOutputBuffer << line << std::endl;
                _textArea->appendFormattedText(line + "\n", USER_INPUT_COLOR, "*-*-Bold");
                _coutMutex.unlock();
            });
        }

        if (!_inputLines.isEmpty()) {
            _cinQueueMutex.lockForWrite();
            if (!_inputLines.isEmpty()) {
                line = _inputLines.dequeue();
                lineRead = true;
            }

            _cinQueueMutex.unlock();
        }

        if (lineRead) {
            break;
        } else {
            sleep(20);
        }
    }

    _cinMutex.lockForWrite();
    _promptActive = false;
    _cinMutex.unlock();
    this->_textArea->scrollToBottom();

    if (_echo) {
        fprintf(stdout, "%s\n", line.c_str());
    }
    return line;
}

void GConsoleWindow::save() {
    saveAs(_lastSaveFileName);
}

void GConsoleWindow::saveAs(const std::string& filename) {
    std::string filenameToUse;
    if (filename.empty()) {
        filenameToUse = GFileChooser::showSaveDialog(
                /* parent */ this->getWidget(),
                /* title */ "",
                getHead(_lastSaveFileName));
    } else {
        filenameToUse = filename;
    }
    if (filenameToUse.empty()) {
        return;
    }

    std::string consoleText = _textArea->getText();
    writeEntireFile(filenameToUse, consoleText);
    _lastSaveFileName = filenameToUse;
}

void GConsoleWindow::saveConfiguration(bool prompt) {
    if (prompt && !GOptionPane::showConfirmDialog(
            /* parent  */  getWidget(),
            /* message */  "Make this the default for future console windows?",
            /* title   */  "Save configuration?")) {
        return;
    }
    std::string configFile = getTempDirectory() + "/" + CONFIG_FILE_NAME;
    std::string configText = "# Stanford C++ library configuration file\n"
            "background=" + _textArea->getBackground() + "\n"
            "foreground=" + getOutputColor() + "\n"
            "font=" + _textArea->getFont() + "\n";
    writeEntireFile(configFile, configText);
}

void GConsoleWindow::selectAll() {
    _textArea->selectAll();
}

void GConsoleWindow::setBackground(int color) {
    GWindow::setBackground(color);   // call super
    _textArea->setBackground(color);
}

void GConsoleWindow::setBackground(const std::string& color) {
    GWindow::setBackground(color);   // call super
    _textArea->setBackground(color);
}

void GConsoleWindow::setClearEnabled(bool clearEnabled) {
    if (_locked || _shutdown) {
        return;
    }
    _clearEnabled = clearEnabled;
}

void GConsoleWindow::setConsoleSize(double width, double height) {
    // TODO: base on text area's preferred size / packing window
    // _textArea->setPreferredSize(width, height);
    // pack();
    setSize(width, height);
}

void GConsoleWindow::setColor(int color) {
    setOutputColor(color);
}

void GConsoleWindow::setColor(const std::string& color) {
    setOutputColor(color);
}

void GConsoleWindow::setEcho(bool echo) {
    if (_locked || _shutdown) {
        return;
    }
    _echo = echo;
}

void GConsoleWindow::setFont(const QFont& font) {
    GWindow::setFont(font);   // call super
    _textArea->setFont(font);
}

void GConsoleWindow::setFont(const std::string& font) {
    GWindow::setFont(font);   // call super
    _textArea->setFont(font);
}

void GConsoleWindow::setForeground(int color) {
    setOutputColor(color);
}

void GConsoleWindow::setForeground(const std::string& color) {
    setOutputColor(color);
}

void GConsoleWindow::setLocationSaved(bool locationSaved) {
    _locationSaved = locationSaved;
}

void GConsoleWindow::setLocked(bool locked) {
    _locked = locked;
}

void GConsoleWindow::setErrorColor(const std::string& errorColor) {
    _errorColor = errorColor;
}

void GConsoleWindow::setOutputColor(int rgb) {
    setOutputColor(GColor::convertRGBToColor(rgb));
}

void GConsoleWindow::setOutputColor(const std::string& outputColor) {
    _outputColor = outputColor;
    _textArea->setForeground(outputColor);

    // go through any past fragments and recolor them to this color

    // select all previous text and change its color
    // (BUG?: also changes user input text to be that color; desired?)
    QTextEdit* textArea = static_cast<QTextEdit*>(this->_textArea->getWidget());
    QTextCursor cursor = textArea->textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(0);
    QTextCharFormat format = cursor.charFormat();
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    format.setForeground(QBrush(GColor::toQColor(outputColor)));
    textArea->setTextCursor(cursor);
    cursor.setCharFormat(format);
    cursor.endEditBlock();
    _textArea->moveCursorToEnd();
}

void GConsoleWindow::setUserInput(const std::string& userInput) {
    _cinMutex.lockForWrite();
    QTextEdit* textArea = static_cast<QTextEdit*>(_textArea->getWidget());

    // delete any current user input
    QTextFragment frag = getUserInputFragment();
    if (frag.isValid()) {
        QTextCursor cursor = textArea->textCursor();
        cursor.beginEditBlock();
        cursor.setPosition(frag.position(), QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, frag.length());
        cursor.removeSelectedText();
        cursor.endEditBlock();
        textArea->setTextCursor(cursor);
    }
    _inputBuffer.clear();
    _cinMutex.unlock();

    // insert the given user input
    for (int i = 0; i < (int) userInput.length(); i++) {
        processUserInputKey(userInput[i]);
    }
}

void GConsoleWindow::showAboutDialog() {
    // this text roughly matches that from old spl.jar message
    static const std::string ABOUT_MESSAGE =
            "<html><p>"
            "Stanford C++ Library version <b>" + version::getCppLibraryVersion() + "</b><br>\n"
            "<br>\n"
            "Libraries originally written by <b>Eric Roberts</b>,<br>\n"
            "with assistance from Julie Zelenski, Keith Schwarz, et al.<br>\n"
            "This version of the library is unofficially maintained by <b>Marty Stepp</b>.<br>\n"
            "<br>\n"
            "See <a href=\"" + version::getCppLibraryDocsUrl() + "\">" + version::getCppLibraryDocsUrl() + "</a> for documentation."
            "</p></html>";
    GOptionPane::showMessageDialog(
                /* parent */   getWidget(),
                /* message */  ABOUT_MESSAGE,
                /* title */    "About Stanford C++ Library",
                /* type */     GOptionPane::MESSAGE_ABOUT);
}

void GConsoleWindow::showColorDialog(bool background) {
    std::string color = GColorChooser::showDialog(
                /* parent */   getWidget(),
                /* title */    "",
                /* initial */  background ? _textArea->getBackground() : _textArea->getForeground());
    if (!color.empty()) {
        if (background) {
            setBackground(color);
        } else {
            setOutputColor(color);
        }
        saveConfiguration();   // prompt to save configuration
    }
}

void GConsoleWindow::showCompareOutputDialog() {
    std::string filename = GFileChooser::showOpenDialog(
                /* parent */ getWidget(),
                /* title  */ "Select an expected output file");
    if (!filename.empty() && fileExists(filename)) {
        compareOutput(filename);
    }
}

void GConsoleWindow::showFontDialog() {
    std::string font = GFontChooser::showDialog(
                /* parent */ getWidget(),
                /* title  */ "",
                /* initialFont */ _textArea->getFont());
    if (!font.empty()) {
        _textArea->setFont(font);
        saveConfiguration();   // prompt to save configuration
    }
}

void GConsoleWindow::showInputScriptDialog() {
    std::string filename = GFileChooser::showOpenDialog(
                /* parent */ getWidget(),
                /* title  */ "Select an input script file");
    if (!filename.empty() && fileExists(filename)) {
        loadInputScript(filename);
    }
}

void GConsoleWindow::showPrintDialog() {
    // TODO
}

void GConsoleWindow::shutdown() {
    const std::string PROGRAM_COMPLETED_TITLE_SUFFIX = " [completed]";
    std::cout.flush();
    std::cerr.flush();
    _shutdown = true;
    _textArea->setEditable(false);
    std::string title = getTitle();
    if (title.find(PROGRAM_COMPLETED_TITLE_SUFFIX) == std::string::npos) {
        setTitle(title + PROGRAM_COMPLETED_TITLE_SUFFIX);
    }
}

// global functions used by ConsoleStreambufQt

namespace stanfordcpplib {
namespace qtgui {

void endLineConsoleQt(bool isStderr) {
    GConsoleWindow::instance()->println(isStderr);
}

std::string getLineConsoleQt() {
    return GConsoleWindow::instance()->readLine();
}

void putConsoleQt(const std::string& str, bool isStderr) {
    GConsoleWindow::instance()->print(str, isStderr);
}

} // namespace qtgui
} // namespace stanfordcpplib
