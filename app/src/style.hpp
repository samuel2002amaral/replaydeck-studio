/* Visual do programa, no mesmo estilo do painel do ReplayDeck: azul bem escuro, caixas com
 * título pequeno, botões com borda, azul para o que está selecionado e verde para "no ar".
 * Vermelho só para programa / gravando. */
#pragma once

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

inline void applyStyle(QApplication &app)
{
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette p;
	p.setColor(QPalette::Window, QColor(0x16, 0x1a, 0x21));
	p.setColor(QPalette::WindowText, QColor(0xe6, 0xe9, 0xef));
	p.setColor(QPalette::Base, QColor(0x0f, 0x12, 0x17));
	p.setColor(QPalette::AlternateBase, QColor(0x1c, 0x21, 0x2a));
	p.setColor(QPalette::Text, QColor(0xe6, 0xe9, 0xef));
	p.setColor(QPalette::Button, QColor(0x2a, 0x31, 0x3e));
	p.setColor(QPalette::ButtonText, QColor(0xdf, 0xe5, 0xee));
	p.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xeb));
	p.setColor(QPalette::HighlightedText, Qt::white);
	p.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x31, 0x3e));
	p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xea, 0xf0));
	p.setColor(QPalette::PlaceholderText, QColor(0x6b, 0x75, 0x87));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5b, 0x64, 0x74));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5b, 0x64, 0x74));
	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5b, 0x64, 0x74));
	app.setPalette(p);

	QFont f("Segoe UI", 9);
	app.setFont(f);

	app.setStyleSheet(R"(
QWidget { color: #e6e9ef; }
QMainWindow, #central, QDialog { background: #161a21; }
QMenuBar { background: #1c212a; border-bottom: 1px solid #2e3541; }
QMenuBar::item { padding: 4px 10px; background: transparent; border-radius: 3px; }
QMenuBar::item:selected { background: #2b3240; }
QMenu { background: #232933; border: 1px solid #363e4c; padding: 3px; }
QMenu::item { padding: 5px 24px 5px 14px; border-radius: 3px; }
QMenu::item:selected { background: #2563eb; color: #ffffff; }
QMenu::separator { height: 1px; background: #363e4c; margin: 4px 2px; }

QPushButton { background: #2a313e; border: 1px solid #3a4252; border-radius: 4px; padding: 4px 12px; min-height: 18px; color: #eef1f5; font-weight: 600; }
QPushButton:hover { background: #343c4b; border-color: #4b5568; }
QPushButton:pressed { background: #20262f; }
QPushButton:disabled { color: #6c7586; background: #232933; border-color: #2c3240; }
QPushButton:checked { background: #2563eb; border-color: #3b74f0; color: #ffffff; }
#primaryButton { background: #2563eb; border-color: #3b74f0; color: #ffffff; }
#primaryButton:hover { background: #2f6df2; }
#greenButton { background: #1d5e36; border-color: #2a7d49; color: #ffffff; }
#greenButton:hover { background: #226b3e; }
#redButton { background: #7d1d17; border-color: #a52a21; color: #ffffff; }
#redButton:hover { background: #8d231c; }
#transButton { min-height: 22px; }
#transButton[stinger="true"] { border-left: 3px solid #c27414; }
#transAddButton { min-height: 22px; background: transparent; border: 1px dashed #4b5568; color: #9aa4b5; }
#transAddButton:hover { color: #eef1f5; border-color: #6b7588; }
#trCard { background: #222833; border: 1px solid #363e4c; border-radius: 4px; }
#trName { font-weight: 700; font-size: 13px; }
#trBadge, #trBadgeStinger { border-radius: 3px; padding: 1px 6px; font-size: 11px; font-weight: 700; color: #ffffff; background: #3a4a66; }
#trBadgeStinger { background: #c27414; }
#ovButton[on="true"] { background: #c27414; border-color: #e08a1e; color: #ffffff; }
#transButton[on="true"] { background: #a52a21; border-color: #c3362b; color: #ffffff; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
  background: #0f1217; border: 1px solid #363e4c; border-radius: 4px; padding: 3px 7px; min-height: 18px;
  selection-background-color: #2563eb;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #2563eb; }
QComboBox QAbstractItemView { background: #232933; border: 1px solid #363e4c; selection-background-color: #2563eb; outline: 0; }
QCheckBox { spacing: 7px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #4b5568; border-radius: 3px; background: #0f1217; }
QCheckBox::indicator:checked { background: #2563eb; border-color: #3b74f0; }

QGroupBox { border: 1px solid #363e4c; border-radius: 5px; margin-top: 10px; padding: 10px 6px 6px 6px; background: #1c212a; }
QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; color: #a3acba; font-size: 7.5pt; font-weight: 700; }

QSplitter::handle { background: #161a21; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
QScrollArea { background: transparent; border: 0; }
#dropMark { background: #3b82f6; border-radius: 1px; }
#tilesHost { background: #1c212a; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #3a4252; border-radius: 3px; min-height: 24px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #3a4252; border-radius: 3px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { width: 0; height: 0; background: transparent; }

#pvwHeader { background: #1f7a45; }
#pgmHeader { background: #b0322a; }
#busTitle { font-weight: 700; color: #ffffff; }
#busFooter { background: #1c212a; color: #a3acba; font-family: Consolas, monospace; font-size: 8pt; }

#panel { background: #1c212a; border: 1px solid #303744; border-radius: 5px; }
#panelBar { background: #232933; border-bottom: 1px solid #303744; }
#panelTitle { font-weight: 700; color: #e6eaf0; }
#muted, #statusText { color: #a3acba; }

#inputTile { background: #222833; border: 1px solid #363e4c; border-radius: 4px; }
#inputTile[bus="preview"] { border-color: #34b36a; }
#inputTile[bus="program"] { border-color: #e0483c; }
#tileHeader { background: #2a313e; }
#tileNum { font-weight: 800; color: #ffffff; }
#tileName { color: #e6eaf0; font-weight: 600; }
#tileGear { color: #c0c7d2; border: 0; padding: 0 4px; }
#tileButton { background: #2a313e; border: 0; border-radius: 0; padding: 0; font-size: 8pt; font-weight: 600; color: #c0c7d2; min-height: 0; }
#tileButton:hover { background: #343c4b; }
#tileButton:checked { background: #2f5d9e; color: #ffffff; }
#tileButton[on="true"] { background: #c27414; color: #ffffff; }
#keyCapture { text-align: left; padding: 3px 8px; }
#keyCapture[on="true"] { background: #c27414; border-color: #e08a1e; color: #ffffff; }
#outButton[on="true"] { background: #a52a21; border-color: #c3362b; color: #ffffff; }
#outButton[on="true"]:hover { background: #b3302a; }
#mixerStrip, #masterStrip { background: #222833; border: 1px solid #363e4c; border-radius: 4px; }
#masterStrip { border-color: #2563eb; }
#mixerTitle { background: #2a313e; color: #e6eaf0; font-weight: 600; font-size: 8pt; border-radius: 3px; }
#mixerTitle[bus="preview"] { background: #1d5e36; }
#mixerTitle[bus="program"] { background: #7d1d17; }
#mixerDb { color: #c0c7d2; font-family: Consolas, monospace; font-size: 8pt; }
QSlider#mixerFader { width: 22px; }
QSlider#mixerFader::groove:vertical { background: #0f1217; width: 4px; border-radius: 2px; }
QSlider#mixerFader::handle:vertical { background: #e6e9ef; height: 12px; margin: 0 -8px; border-radius: 2px; }
QSlider#mixerFader::handle:vertical:hover { background: #ffffff; }

QTabWidget::pane { border: 1px solid #303744; background: #1c212a; top: -1px; }
QTabBar::tab { background: #232933; color: #a3acba; padding: 6px 14px; border: 1px solid #303744; border-bottom: 0; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; font-weight: 600; }
QTabBar::tab:selected { background: #1c212a; color: #ffffff; border-top: 2px solid #2563eb; }
QTabBar::tab:hover { color: #e6e9ef; }

QSlider#tbar::groove:vertical { background: #0f1217; border: 1px solid #3a4252; width: 12px; border-radius: 4px; }
QSlider#tbar::handle:vertical { background: #e6e9ef; height: 12px; margin: 0 -10px; border-radius: 3px; }
QSlider::groove:horizontal { background: #2e3541; height: 4px; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #2563eb; border-radius: 2px; }
QSlider::handle:horizontal { background: #e6eaf0; width: 12px; margin: -5px 0; border-radius: 6px; border: 2px solid #2563eb; }

#bottomBar { background: #161a21; }
QStatusBar { background: #0f1217; border-top: 1px solid #2e3541; }
QStatusBar::item { border: 0; }
#statusText { font-family: Consolas, monospace; font-size: 8pt; padding: 0 8px; }
QToolTip { background: #2a313e; color: #e6eaf0; border: 1px solid #3a4252; padding: 4px; }
QListWidget { background: #0f1217; border: 1px solid #2e3541; border-radius: 4px; outline: 0; padding: 3px; }
QListWidget::item { padding: 7px 10px; border-radius: 3px; margin: 1px 0; }
QListWidget::item:hover { background: #2a313e; }
QListWidget::item:selected { background: #2563eb; color: #ffffff; }
#typeList { font-size: 9.5pt; }
QDialogButtonBox QPushButton { min-width: 90px; min-height: 22px; }
#dialogTitle { font-size: 12pt; font-weight: 700; color: #ffffff; }
#addNewButton { min-height: 48px; background: transparent; border: 1px dashed #4b5568; border-radius: 6px; font-size: 10pt; color: #e6eaf0; }
#addNewButton:hover { background: #161c28; border-color: #2563eb; }
#existingCard { background: #222833; border: 1px solid #363e4c; border-radius: 6px; padding: 0; text-align: left; min-height: 126px; max-height: 126px; min-width: 178px; max-width: 178px; }
#existingCard:hover { border-color: #4b5568; }
#existingCard:checked { background: #172544; border-color: #2563eb; }
#audioPlaceholder { background: #0f1217; border: 0; border-radius: 0; color: #a3acba; font-weight: 600; }
)");
}
