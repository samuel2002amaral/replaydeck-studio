#pragma once
/* Idiomas do ReplayDeck.
   O texto-fonte é o português do código: tl("Gravar") devolve a tradução do idioma atual.
   As traduções ficam em data/lang/<código>.json  { "texto em português": "tradução" }.
   N_("...") só marca um texto para tradução (listas/arrays); traduza depois com tl(ptr). */
#include <QString>
#include <QStringList>
#include <vector>

class QWidget;

#define N_(s) s

namespace i18n {

struct Lang {
	const char *code; /* "pt-BR", "en", ... */
	const char *name; /* nome no próprio idioma */
};

const std::vector<Lang> &languages();

/* "auto" (segue o idioma do OBS) ou um código da lista */
QString setting();
/* código em uso agora */
QString current();
/* troca o idioma na hora: salva, recarrega e retraduz os painéis/janelas abertos */
void setLanguage(const QString &code);

/* janelas e painéis que devem ser retraduzidos quando o idioma muda */
void track(QWidget *root);
/* chamado depois de retraduzir (ex.: o painel refaz os nomes dos grupos) */
void onChanged(QWidget *owner, void (*fn)(QWidget *owner));

/* dica com números (%1...): guarda o texto-fonte para retraduzir quando o idioma muda */
void setTip(QWidget *w, const char *pt, const QStringList &args);

QString tr(const char *pt);
const char *trc(const char *pt); /* ponteiro estável (para o OBS) */

} // namespace i18n

inline QString tl(const char *pt)
{
	return i18n::tr(pt);
}
inline QString tl(const QString &pt)
{
	return i18n::tr(pt.toUtf8().constData());
}
inline const char *tlc(const char *pt)
{
	return i18n::trc(pt);
}
