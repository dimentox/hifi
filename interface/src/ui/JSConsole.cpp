//
//  JSConsole.cpp
//  interface/src/ui
//
//  Created by Ryan Huffman on 05/12/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QKeyEvent>
#include <QLabel>
#include <QScrollBar>

#include "Application.h"
#include "ScriptHighlighting.h"

#include "JSConsole.h"

const int NO_CURRENT_HISTORY_COMMAND = -1;
const int MAX_HISTORY_SIZE = 64;

const QString COMMAND_STYLE = "color: #266a9b;";

const QString RESULT_SUCCESS_STYLE = "color: #677373;";
const QString RESULT_ERROR_STYLE = "color: #d13b22;";

const QString GUTTER_PREVIOUS_COMMAND = "<span style=\"color: #57b8bb;\">&lt;</span>";
const QString GUTTER_ERROR = "<span style=\"color: #d13b22;\">X</span>";

JSConsole::JSConsole(QWidget* parent, ScriptEngine* scriptEngine) :
    QWidget(parent),
    _ui(new Ui::Console),
    _currentCommandInHistory(NO_CURRENT_HISTORY_COMMAND),
    _commandHistory(),
    _scriptEngine(scriptEngine) {

    _ui->setupUi(this);
    _ui->promptTextEdit->setLineWrapMode(QTextEdit::NoWrap);
    _ui->promptTextEdit->setWordWrapMode(QTextOption::NoWrap);
    _ui->promptTextEdit->installEventFilter(this);

    QFile styleSheet(Application::resourcesPath() + "styles/console.qss");
    if (styleSheet.open(QIODevice::ReadOnly)) {
        QDir::setCurrent(Application::resourcesPath());
        setStyleSheet(styleSheet.readAll());
    }

    connect(_ui->scrollArea->verticalScrollBar(), SIGNAL(rangeChanged(int, int)), this, SLOT(scrollToBottom()));
    connect(_ui->promptTextEdit, SIGNAL(textChanged()), this, SLOT(resizeTextInput()));


    if (_scriptEngine == NULL) {
        _scriptEngine = Application::getInstance()->loadScript(QString(), false);
    }

    connect(_scriptEngine, SIGNAL(evaluationFinished(QScriptValue, bool)),
            this, SLOT(handleEvalutationFinished(QScriptValue, bool)));
    connect(_scriptEngine, SIGNAL(printedMessage(const QString&)), this, SLOT(handlePrint(const QString&)));

    resizeTextInput();
}

JSConsole::~JSConsole() {
    delete _ui;
}

void JSConsole::executeCommand(const QString& command) {
    _commandHistory.prepend(command);
    if (_commandHistory.length() > MAX_HISTORY_SIZE) {
        _commandHistory.removeLast();
    }

    _ui->promptTextEdit->setDisabled(true);

    appendMessage(">", "<span style='" + COMMAND_STYLE + "'>" + command.toHtmlEscaped() + "</span>");

    QMetaObject::invokeMethod(_scriptEngine, "evaluate", Q_ARG(const QString&, command));

    resetCurrentCommandHistory();
}

void JSConsole::handleEvalutationFinished(QScriptValue result, bool isException) {
    _ui->promptTextEdit->setDisabled(false);

    // Make sure focus is still on this window - some commands are blocking and can take awhile to execute.
    if (window()->isActiveWindow()) {
        _ui->promptTextEdit->setFocus();
    }
    
    QString gutter = (isException || result.isError()) ? GUTTER_ERROR : GUTTER_PREVIOUS_COMMAND;
    QString resultColor = (isException || result.isError()) ? RESULT_ERROR_STYLE : RESULT_SUCCESS_STYLE;
    QString resultStr = "<span style='" + resultColor + "'>" + result.toString().toHtmlEscaped()  + "</span>";
    appendMessage(gutter, resultStr);
}

void JSConsole::handlePrint(const QString& message) {
    appendMessage("", message);
}

void JSConsole::mouseReleaseEvent(QMouseEvent* event) {
    _ui->promptTextEdit->setFocus();
}

void JSConsole::showEvent(QShowEvent* event) {
    _ui->promptTextEdit->setFocus();
}

bool JSConsole::eventFilter(QObject* sender, QEvent* event) {
    if (sender == _ui->promptTextEdit) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            int key = keyEvent->key();

            if ((key == Qt::Key_Return || key == Qt::Key_Enter)) {
                if (keyEvent->modifiers() & Qt::ShiftModifier) {
                    // If the shift key is being used then treat it as a regular return/enter.  If this isn't done,
                    // a new QTextBlock isn't created.
                    keyEvent->setModifiers(keyEvent->modifiers() & ~Qt::ShiftModifier);
                } else {
                    QString command = _ui->promptTextEdit->toPlainText().trimmed();

                    if (!command.isEmpty()) {
                        QTextCursor cursor = _ui->promptTextEdit->textCursor();
                        _ui->promptTextEdit->clear();

                        executeCommand(command);
                    }

                    return true;
                }
            } else if (key == Qt::Key_Down) {
                // Go to the next command in history if the cursor is at the last line of the current command.
                int blockNumber = _ui->promptTextEdit->textCursor().blockNumber();
                int blockCount = _ui->promptTextEdit->document()->blockCount();
                if (blockNumber == blockCount - 1) {
                    setToNextCommandInHistory();
                    return true;
                }
            } else if (key == Qt::Key_Up) {
                // Go to the previous command in history if the cursor is at the first line of the current command.
                int blockNumber = _ui->promptTextEdit->textCursor().blockNumber();
                if (blockNumber == 0) {
                    setToPreviousCommandInHistory();
                    return true;
                }
            }
        }
    }
    return false;
}

void JSConsole::setToNextCommandInHistory() {
    if (_currentCommandInHistory >= 0) {
        _currentCommandInHistory--;
        if (_currentCommandInHistory == NO_CURRENT_HISTORY_COMMAND) {
            setAndSelectCommand(_rootCommand);
        } else {
            setAndSelectCommand(_commandHistory[_currentCommandInHistory]);
        }
    }
}

void JSConsole::setToPreviousCommandInHistory() {
    if (_currentCommandInHistory < (_commandHistory.length() - 1)) {
        if (_currentCommandInHistory == NO_CURRENT_HISTORY_COMMAND) {
            _rootCommand = _ui->promptTextEdit->toPlainText();
        }
        _currentCommandInHistory++;
        setAndSelectCommand(_commandHistory[_currentCommandInHistory]);
    }
}

void JSConsole::resetCurrentCommandHistory() {
    _currentCommandInHistory = NO_CURRENT_HISTORY_COMMAND;
}

void JSConsole::resizeTextInput() {
    _ui->promptTextEdit->setFixedHeight(_ui->promptTextEdit->document()->size().height());
    _ui->promptTextEdit->updateGeometry();
}

void JSConsole::setAndSelectCommand(const QString& text) {
    QTextCursor cursor = _ui->promptTextEdit->textCursor();
    cursor.select(QTextCursor::Document);
    cursor.deleteChar();
    cursor.insertText(text);
    cursor.movePosition(QTextCursor::End);
}

void JSConsole::scrollToBottom() {
    QScrollBar* scrollBar = _ui->scrollArea->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void JSConsole::appendMessage(const QString& gutter, const QString& message) {
    QWidget* logLine = new QWidget(_ui->logArea);
    QHBoxLayout* layout = new QHBoxLayout(logLine);
    layout->setMargin(0);
    layout->setSpacing(4);

    QLabel* gutterLabel = new QLabel(logLine);
    QLabel* messageLabel = new QLabel(logLine);

    gutterLabel->setFixedWidth(16);
    gutterLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    gutterLabel->setStyleSheet("font-size: 14px; font-family: Inconsolata, Lucida Console, Andale Mono, Monaco;");
    messageLabel->setStyleSheet("font-size: 14px; font-family: Inconsolata, Lucida Console, Andale Mono, Monaco;");

    gutterLabel->setText(gutter);
    messageLabel->setText(message);

    layout->addWidget(gutterLabel);
    layout->addWidget(messageLabel);
    logLine->setLayout(layout);

    layout->setAlignment(gutterLabel, Qt::AlignTop);

    layout->setStretch(0, 0);
    layout->setStretch(1, 1);

    _ui->logArea->layout()->addWidget(logLine);

    _ui->logArea->updateGeometry();
    scrollToBottom();
}
