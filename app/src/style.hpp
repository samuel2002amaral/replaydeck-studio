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
	p.setColor(QPalette::Window, QColor(0x0f, 0x12, 0x18));
	p.setColor(QPalette::WindowText, QColor(0xd7, 0xdc, 0xe4));
	p.setColor(QPalette::Base, QColor(0x0b, 0x0e, 0x13));
	p.setColor(QPalette::AlternateBase, QColor(0x14, 0x18, 0x21));
	p.setColor(QPalette::Text, QColor(0xd7, 0xdc, 0xe4));
	p.setColor(QPalette::Button, QColor(0x1b, 0x21, 0x30));
	p.setColor(QPalette::ButtonText, QColor(0xdf, 0xe5, 0xee));
	p.setColor(QPalette::Highlight, QColor(0x25, 0x63, 0xeb));
	p.setColor(QPalette::HighlightedText, Qt::white);
	p.setColor(QPalette::ToolTipBase, QColor(0x1b, 0x21, 0x30));
	p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xea, 0xf0));
	p.setColor(QPalette::PlaceholderText, QColor(0x6b, 0x75, 0x87));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5b, 0x64, 0x74));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5b, 0x64, 0x74));
	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x5b, 0x64, 0x74));
	app.setPalette(p);

	QFont f("Segoe UI", 9);
	app.setFont(f);

	app.setStyleSheet(R"(
QWidget { color: #d7dce4; }
QMainWindow, #central, QDialog { background: #0f1218; }
QMenuBar { background: #12161e; border-bottom: 1px solid #232a36; }
QMenuBar::item { padding: 4px 10px; background: transparent; border-radius: 3px; }
QMenuBar::item:selected { background: #1f2635; }
QMenu { background: #151a24; border: 1px solid #2a3140; padding: 3px; }
QMenu::item { padding: 5px 24px 5px 14px; border-radius: 3px; }
QMenu::item:selected { background: #2563eb; color: #ffffff; }
QMenu::separator { height: 1px; background: #2a3140; margin: 4px 2px; }

QPushButton { background: #1b2130; border: 1px solid #2c3445; border-radius: 4px; padding: 4px 12px; min-height: 18px; color: #dfe5ee; font-weight: 600; }
QPushButton:hover { background: #232b3c; border-color: #3a4458; }
QPushButton:pressed { background: #161b27; }
QPushButton:disabled { color: #5b6474; background: #151a24; border-color: #222836; }
QPushButton:checked { background: #2563eb; border-color: #3b74f0; color: #ffffff; }
#primaryButton { background: #2563eb; border-color: #3b74f0; color: #ffffff; }
#primaryButton:hover { background: #2f6df2; }
#greenButton { background: #1d5e36; border-color: #2a7d49; color: #ffffff; }
#greenButton:hover { background: #226b3e; }
#redButton { background: #7d1d17; border-color: #a52a21; color: #ffffff; }
#redButton:hover { background: #8d231c; }
#transButton { min-height: 22px; }
#ovButton[on="true"], #transButton[on="true"] { background: #a52a21; border-color: #c3362b; color: #ffffff; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
  background: #0b0e13; border: 1px solid #2a3140; border-radius: 4px; padding: 3px 7px; min-height: 18px;
  selection-background-color: #2563eb;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #2563eb; }
QComboBox QAbstractItemView { background: #151a24; border: 1px solid #2a3140; selection-background-color: #2563eb; outline: 0; }
QCheckBox { spacing: 7px; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #3a4458; border-radius: 3px; background: #0b0e13; }
QCheckBox::indicator:checked { background: #2563eb; border-color: #3b74f0; }

QGroupBox { border: 1px solid #2a3140; border-radius: 5px; margin-top: 10px; padding: 10px 6px 6px 6px; background: #12161e; }
QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; color: #8b95a7; font-size: 7.5pt; font-weight: 700; }

QSplitter::handle { background: #0f1218; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
QScrollArea { background: transparent; border: 0; }
#tilesHost { background: #12161e; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2c3445; border-radius: 3px; min-height: 24px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2c3445; border-radius: 3px; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { width: 0; height: 0; background: transparent; }

#pvwHeader { background: #151a24; border-top: 2px solid #2e9d55; }
#pgmHeader { background: #151a24; border-top: 2px solid #c3362b; }
#busTitle { font-weight: 700; color: #ffffff; }
#busFooter { background: #12161e; color: #8b95a7; font-family: Consolas, monospace; font-size: 8pt; }

#panel { background: #12161e; border: 1px solid #262c38; border-radius: 5px; }
#panelBar { background: #151a24; border-bottom: 1px solid #262c38; }
#panelTitle { font-weight: 700; color: #e6eaf0; }
#muted, #statusText { color: #8b95a7; }

#inputTile { background: #161b26; border: 1px solid #2a3140; border-radius: 4px; }
#inputTile[bus="preview"] { border-color: #2e9d55; }
#inputTile[bus="program"] { border-color: #c3362b; }
#tileHeader { background: #1b2130; }
#tileHeader[bus="preview"] { background: #1d5e36; }
#tileHeader[bus="program"] { background: #7d1d17; }
#tileNum { font-weight: 800; color: #ffffff; }
#tileName { color: #e6eaf0; font-weight: 600; }
#tileGear { color: #aab3c2; border: 0; padding: 0 4px; }
#tileButton { background: #1b2130; border: 0; border-radius: 0; padding: 0; font-size: 8pt; font-weight: 600; color: #aab3c2; min-height: 0; }
#tileButton:hover { background: #232b3c; }
#tileButton:checked { background: #1f3f7a; color: #ffffff; }
#tileButton[on="true"] { background: #a52a21; color: #ffffff; }

QTabWidget::pane { border: 1px solid #262c38; background: #12161e; top: -1px; }
QTabBar::tab { background: #151a24; color: #8b95a7; padding: 6px 14px; border: 1px solid #262c38; border-bottom: 0; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; font-weight: 600; }
QTabBar::tab:selected { background: #12161e; color: #ffffff; border-top: 2px solid #2563eb; }
QTabBar::tab:hover { color: #d7dce4; }

QSlider#tbar::groove:vertical { background: #0b0e13; border: 1px solid #2c3445; width: 12px; border-radius: 4px; }
QSlider#tbar::handle:vertical { background: #d7dce4; height: 12px; margin: 0 -10px; border-radius: 3px; }
QSlider::groove:horizontal { background: #232a36; height: 4px; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #2563eb; border-radius: 2px; }
QSlider::handle:horizontal { background: #e6eaf0; width: 12px; margin: -5px 0; border-radius: 6px; border: 2px solid #2563eb; }

#bottomBar { background: #0f1218; }
QStatusBar { background: #0b0e13; border-top: 1px solid #232a36; }
QStatusBar::item { border: 0; }
#statusText { font-family: Consolas, monospace; font-size: 8pt; padding: 0 8px; }
QToolTip { background: #1b2130; color: #e6eaf0; border: 1px solid #2c3445; padding: 4px; }
QListWidget { background: #0b0e13; border: 1px solid #232a36; border-radius: 4px; outline: 0; padding: 3px; }
QListWidget::item { padding: 7px 10px; border-radius: 3px; margin: 1px 0; }
QListWidget::item:hover { background: #1b2130; }
QListWidget::item:selected { background: #2563eb; color: #ffffff; }
#typeList { font-size: 9.5pt; }
QDialogButtonBox QPushButton { min-width: 90px; min-height: 22px; }
)");
}
