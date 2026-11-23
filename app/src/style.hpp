/* Visual padrão de programa de transmissão: cinza escuro, sem enfeites.
 * Cores só com significado: verde = prévia, vermelho = programa/gravando, azul = selecionado. */
#pragma once

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

inline void applyStyle(QApplication &app)
{
	app.setStyle(QStyleFactory::create("Fusion"));
	QPalette p;
	p.setColor(QPalette::Window, QColor(0x1b, 0x1b, 0x1b));
	p.setColor(QPalette::WindowText, QColor(0xd4, 0xd4, 0xd4));
	p.setColor(QPalette::Base, QColor(0x14, 0x14, 0x14));
	p.setColor(QPalette::AlternateBase, QColor(0x20, 0x20, 0x20));
	p.setColor(QPalette::Text, QColor(0xd4, 0xd4, 0xd4));
	p.setColor(QPalette::Button, QColor(0x2a, 0x2a, 0x2a));
	p.setColor(QPalette::ButtonText, QColor(0xd4, 0xd4, 0xd4));
	p.setColor(QPalette::Highlight, QColor(0x2f, 0x62, 0xc9));
	p.setColor(QPalette::HighlightedText, Qt::white);
	p.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x2a, 0x2a));
	p.setColor(QPalette::ToolTipText, QColor(0xe0, 0xe0, 0xe0));
	p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6a, 0x6a, 0x6a));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6a, 0x6a, 0x6a));
	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6a, 0x6a, 0x6a));
	app.setPalette(p);

	QFont f("Segoe UI", 9);
	app.setFont(f);

	app.setStyleSheet(R"(
QWidget { color: #d4d4d4; }
QMainWindow, #central { background: #1b1b1b; }
QMenuBar { background: #202020; border-bottom: 1px solid #333333; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: #2f2f2f; }
QMenu { background: #252525; border: 1px solid #3c3c3c; }
QMenu::item { padding: 5px 24px 5px 16px; }
QMenu::item:selected { background: #2f62c9; color: #ffffff; }
QMenu::separator { height: 1px; background: #3c3c3c; margin: 4px 0; }

QPushButton { background: #2a2a2a; border: 1px solid #3c3c3c; border-radius: 2px; padding: 3px 12px; min-height: 18px; }
QPushButton:hover { background: #333333; border-color: #4a4a4a; }
QPushButton:pressed { background: #232323; }
QPushButton:disabled { color: #6a6a6a; background: #242424; border-color: #303030; }
QPushButton:checked { background: #2f62c9; border-color: #2f62c9; color: #ffffff; }
#transButton { font-weight: 600; }
#ovButton[on="true"], #transButton[on="true"] { background: #8f1f18; border-color: #b3261e; color: #ffffff; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
  background: #141414; border: 1px solid #3c3c3c; border-radius: 2px; padding: 2px 6px; min-height: 18px;
  selection-background-color: #2f62c9;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #2f62c9; }
QComboBox QAbstractItemView { background: #252525; border: 1px solid #3c3c3c; selection-background-color: #2f62c9; }
QCheckBox::indicator { width: 13px; height: 13px; }
QGroupBox { border: 1px solid #333333; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #a0a0a0; }

QSplitter::handle { background: #1b1b1b; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
QScrollArea, #tilesHost { background: #202020; }
QScrollBar:vertical { background: #1b1b1b; width: 12px; }
QScrollBar::handle:vertical { background: #3a3a3a; min-height: 24px; }
QScrollBar:horizontal { background: #1b1b1b; height: 12px; }
QScrollBar::handle:horizontal { background: #3a3a3a; min-width: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }

#pvwHeader { background: #2a2a2a; border-top: 2px solid #3d8b40; }
#pgmHeader { background: #2a2a2a; border-top: 2px solid #b3261e; }
#busTitle { font-weight: 600; color: #ffffff; }
#busFooter { background: #232323; color: #8a8a8a; font-family: Consolas, monospace; font-size: 8pt; }

#panel { background: #202020; border: 1px solid #333333; }
#panelBar { background: #2a2a2a; border-bottom: 1px solid #333333; }
#panelTitle { font-weight: 600; color: #e0e0e0; }
#muted, #statusText { color: #8a8a8a; }

#inputTile { background: #262626; border: 1px solid #333333; }
#inputTile[bus="preview"] { border-color: #3d8b40; }
#inputTile[bus="program"] { border-color: #b3261e; }
#tileHeader { background: #2f2f2f; }
#tileHeader[bus="preview"] { background: #2e6b31; }
#tileHeader[bus="program"] { background: #8f1f18; }
#tileNum { font-weight: 700; color: #ffffff; }
#tileName { color: #e0e0e0; }
#tileGear { color: #b0b0b0; border: 0; padding: 0 4px; }
#tileButton { background: #262626; border: 0; border-radius: 0; padding: 0; font-size: 8pt; color: #b0b0b0; }
#tileButton:hover { background: #303030; }
#tileButton:checked { background: #2a2a2a; color: #e0e0e0; }
#tileButton[on="true"] { background: #8f1f18; color: #ffffff; }

QTabWidget::pane { border: 1px solid #333333; background: #1e1e1e; top: -1px; }
QTabBar::tab { background: #2a2a2a; color: #a0a0a0; padding: 5px 14px; border: 1px solid #333333; border-bottom: 0; margin-right: -1px; }
QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; border-top: 2px solid #2f62c9; }

QSlider#tbar::groove:vertical { background: #141414; border: 1px solid #3c3c3c; width: 12px; }
QSlider#tbar::handle:vertical { background: #bdbdbd; height: 10px; margin: 0 -10px; }

#bottomBar { background: #1b1b1b; }
QStatusBar { background: #181818; border-top: 1px solid #333333; }
QStatusBar::item { border: 0; }
#statusText { font-family: Consolas, monospace; font-size: 8pt; padding: 0 8px; }
QToolTip { background: #2a2a2a; color: #e0e0e0; border: 1px solid #3c3c3c; padding: 4px; }
QListWidget { background: #161616; border: 1px solid #333333; }
QListWidget::item { padding: 5px 8px; }
QListWidget::item:selected { background: #2f62c9; color: #ffffff; }
QDialog { background: #252525; }
)");
}
