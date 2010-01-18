/*
 *  Copyright (C) 2010  Alexandre Courbot
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "core/Database.h"
#include "core/TextTools.h"
#include "core/XmlParserHelper.h"
#include "sqlite/qsql_sqlite.h"
#include "core/kanjidic2/Kanjidic2Entry.h"

#include <QLocale>
#include <QStringList>
#include <QMap>

#include <QtDebug>

class Kanji {
private:
	static const QStringList _validReadings;
	static QSqlQuery _insertEntryQuery;
	static QSqlQuery _insertReadingQuery;
	static QSqlQuery _insertReadingTextQuery;
	static QSqlQuery _insertMeaningQuery;
	static QSqlQuery _insertMeaningTextQuery;
	static QSqlQuery _insertNanoriQuery;
	static QSqlQuery _insertNanoriTextQuery;
	static QSqlQuery _updateJLPTLevels;
	
public:
	static QStringList languages;
	int id;
	int grade;
	int stroke_count;
	int freq;
	int jlpt;
	QStringList skip;
	// TODO Add a rmgroup parameter to allow grouping of readings and meanings
	// Associate the reading types to the readings
	QMap<QString, QStringList> readings;
	// Associate the language to the meaning
	QMap<QString, QStringList> meanings;
	QStringList nanori;
	
	static void initializeQueries(QSqlDatabase &database);

	Kanji() : id(0), grade(0), stroke_count(0), freq(0), jlpt(0) {}
	bool insertIntoDatabase();
	static bool updateJLPTLevels(const QString &fName, int level);
};

const QStringList Kanji::_validReadings(QString("ja_on,ja_kun").split(','));
QStringList Kanji::languages(QString("en").split(','));
QSqlQuery Kanji::_insertEntryQuery;
QSqlQuery Kanji::_insertReadingQuery;
QSqlQuery Kanji::_insertReadingTextQuery;
QSqlQuery Kanji::_insertMeaningQuery;
QSqlQuery Kanji::_insertMeaningTextQuery;
QSqlQuery Kanji::_insertNanoriQuery;
QSqlQuery Kanji::_insertNanoriTextQuery;
QSqlQuery Kanji::_updateJLPTLevels;

void Kanji::initializeQueries(QSqlDatabase& database)
{
#define PREPQUERY(query, text) query = QSqlQuery(database); query.prepare(text)
	PREPQUERY(_insertEntryQuery, "insert into entries values(?, ?, ?, ?, ?)");
	PREPQUERY(_insertReadingQuery, "insert into reading values(?, ?, ?)");
	PREPQUERY(_insertReadingTextQuery, "insert into readingText values(?)");
	PREPQUERY(_insertMeaningQuery, "insert into meaning values(?, ?, ?)");
	PREPQUERY(_insertMeaningTextQuery, "insert into meaningText values(?)");
	PREPQUERY(_insertNanoriQuery, "insert into nanori values(?, ?)");
	PREPQUERY(_insertNanoriTextQuery, "insert into nanoriText values(?)");
	PREPQUERY(_updateJLPTLevels, "update entries set jlpt = ? where id = ?");
#undef PREPQUERY
}

#define BIND(query, val) query.addBindValue(val)
#define AUTO_BIND(query, val, nval) if (val == nval) BIND(query, QVariant::Int); else BIND(query, val)
#define EXEC(query) if (!query.exec()) { qDebug() << query.lastError().text(); return false; }

/**
 * Returns true if the entry has successfully been inserted, false otherwise.
 */
bool Kanji::insertIntoDatabase()
{
	AUTO_BIND(_insertEntryQuery, id, 0);
	AUTO_BIND(_insertEntryQuery, grade, 0);
	AUTO_BIND(_insertEntryQuery, stroke_count, 0);
	AUTO_BIND(_insertEntryQuery, freq, 0);
	AUTO_BIND(_insertEntryQuery, jlpt, 0);
	EXEC(_insertEntryQuery);

	foreach (const QString &readingType, _validReadings) {
		if (readings.contains(readingType)) {
			foreach (const QString &reading, readings[readingType]) {
				// TODO factorize identical readings! Record the row id into a hash table
				BIND(_insertReadingTextQuery, reading);
				EXEC(_insertReadingTextQuery);
				BIND(_insertReadingQuery, _insertReadingTextQuery.lastInsertId());
				BIND(_insertReadingQuery, id);
				// TODO reading type should be a tinyInt, not a string!
				BIND(_insertReadingQuery, readingType);
				EXEC(_insertReadingQuery);
			}
		}
	}
	
	foreach (const QString &lang, languages) {
		if (meanings.contains(lang)) {
			foreach (const QString &meaning, meanings[lang]) {
				// TODO factorize identical meanings! Record the row id into a hash table
				BIND(_insertMeaningTextQuery, meaning);
				EXEC(_insertMeaningTextQuery);
				BIND(_insertMeaningQuery, _insertMeaningTextQuery.lastInsertId());
				BIND(_insertMeaningQuery, id);
				BIND(_insertMeaningQuery, lang);
				EXEC(_insertMeaningQuery);
			}
		}
	}
	
	foreach (const QString &n, nanori) {
		// TODO factorize identical nanoris! Record the row id into a hash table
		BIND(_insertNanoriTextQuery, n);
		EXEC(_insertNanoriTextQuery);
		BIND(_insertNanoriQuery, _insertNanoriTextQuery.lastInsertId());
		BIND(_insertNanoriQuery, id);
		EXEC(_insertNanoriQuery);
	}
	return true;
}

bool Kanji::updateJLPTLevels(const QString &fName, int level)
{
	QFile file(fName);
	if (!file.open(QFile::ReadOnly | QFile::Text)) return false;
	char line[50];
	int lineSize;
	while ((lineSize = file.readLine(line, 50)) != -1) {
		if (lineSize == 0) continue;
		if (line[lineSize - 1] == '\n') line[lineSize - 1] = 0;
		BIND(_updateJLPTLevels, level);
		BIND(_updateJLPTLevels, TextTools::singleCharToUnicode(QString::fromUtf8(line)));
		EXEC(_updateJLPTLevels);
	}
}

static bool process_kanjidic2(QXmlStreamReader &reader)
{
	DOCUMENT_BEGIN(reader)
		TAG(kanjidic2)
			TAG_PRE(character)
			Kanji kanji;
			TAG_BEGIN(character)
				TAG(literal)
				CHARACTERS
					int kanjiCode = TextTools::singleCharToUnicode(TEXT);
					kanji.id = kanjiCode;
				DONE
				ENDTAG
				TAG(misc)
					TAG(grade)
					CHARACTERS
						kanji.grade = TEXT.toInt();
					DONE
					ENDTAG
					TAG(stroke_count)
					CHARACTERS
						kanji.stroke_count = TEXT.toInt();
					DONE
					ENDTAG
					TAG(freq)
					CHARACTERS
						kanji.freq = TEXT.toInt();
					DONE
					ENDTAG
					TAG(jlpt)
					CHARACTERS
						kanji.jlpt = TEXT.toInt();
					DONE
					ENDTAG
				ENDTAG
				TAG(reading_meaning)
					TAG(rmgroup)
						TAG_PRE(reading)
							QString r_type(ATTR("r_type"));
						TAG_BEGIN(reading)
						CHARACTERS
							kanji.readings[r_type] << TEXT;
						DONE
						ENDTAG
						TAG_PRE(meaning)
							QString lang;
							if (HAS_ATTR("m_lang")) lang = ATTR("m_lang");
							else lang = "en";
						TAG_BEGIN(meaning)
							kanji.meanings[lang] << TEXT;
						CHARACTERS
						DONE
						ENDTAG
					ENDTAG
					TAG(nanori)
					CHARACTERS
						kanji.nanori << TEXT;
					DONE
					ENDTAG
				ENDTAG
			TAG_POST
			if (!kanji.insertIntoDatabase()) return true;
			DONE
			TAG(header)
			ENDTAG
		ENDTAG
	DOCUMENT_END
}

static bool process_kanjivg_strokegr(QXmlStreamReader &reader)
{
	TAG_BEGIN(strokegr)
		TAG_PRE(strokegr)
			process_kanjivg_strokegr(reader);
		DONE
		TAG(stroke)
		ENDTAG
	TAG_POST
	return false;
}

static bool process_kanjivg(QXmlStreamReader &reader)
{
	DOCUMENT_BEGIN(reader)
		TAG(kanjis)
			TAG_PRE(kanji)
				QString midashi(ATTR("midashi"));
			TAG_BEGIN(kanji)
				TAG_PRE(strokegr)
					process_kanjivg_strokegr(reader);
				DONE
			TAG_POST
			DONE
		ENDTAG
	DOCUMENT_END
}

void create_tables()
{
	QSqlQuery query;
	query.exec("create table info(version INT)");
	query.prepare("insert into info values(?)");
	query.addBindValue(KANJIDIC2DB_REVISION);
	query.exec();
	query.exec("create table entries(id INTEGER PRIMARY KEY, grade TINYINT, strokeCount TINYINT, frequency SMALLINT, jlpt TINYINT)");
	query.exec("create table reading(docid INTEGER PRIMARY KEY, entry INTEGER SECONDARY KEY REFERENCES entries, type TEXT)");
	query.exec("create virtual table readingText using fts3(reading, TOKENIZE katakana)");
	query.exec("create table meaning(docid INTEGER PRIMARY KEY, entry INTEGER SECONDARY KEY REFERENCES entries, lang TEXT)");
	query.exec("create virtual table meaningText using fts3(reading)");
	query.exec("create table nanori(docid INTEGER PRIMARY KEY, entry INTEGER SECONDARY KEY REFERENCES entries)");
	query.exec("create virtual table nanoriText using fts3(reading, TOKENIZE katakana)");

	query.exec("create table strokeGroups(kanji INTEGER, parentGroup INTEGER SECONDARY KEY REFERENCES strokeGroups, number TINYINT, element INTEGER, original INTEGER)");
	query.exec("create table strokes(parentGroup INTEGER SECONDARY KEY REFERENCES strokeGroups, strokeType INTEGER, path TEXT)");
	query.exec("create table skip(entry INTEGER, type TINYINT, c1 TINYINT, c2 TINYINT)");
}

void create_indexes()
{
	QSqlQuery query;
	query.exec("create index idx_entries_frequency on entries(frequency)");
	query.exec("create index idx_jlpt on entries(jlpt)");
	query.exec("create index idx_reading_entry on reading(entry)");
	query.exec("create index idx_meaning_entry on meaning(entry)");
	query.exec("create index idx_nanori_entry on nanori(entry)");
	query.exec("create index idx_strokeGroups_kanji on strokeGroups(kanji)");
	query.exec("create index idx_strokeGroups_parentGroup on strokeGroups(parentGroup)");
	query.exec("create index idx_strokeGroups_element on strokeGroups(element)");
	query.exec("create index idx_strokeGroups_original on strokeGroups(original)");
	query.exec("create index idx_strokes_parentGroup on strokes(parentGroup)");
	query.exec("create index idx_strokes_strokeType on strokes(strokeType)");
	query.exec("create index idx_skip on skip(entry)");
	query.exec("create index idx_skip_type on skip(type, c1, c2)");
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	
	// Open the database to write to
	QSQLiteDriver *driver = new QSQLiteDriver();
	QSqlDatabase database(QSqlDatabase::addDatabase(driver));
	database.setDatabaseName("kanjidic2test.db");
	if (!database.open()) {
		qFatal("Cannot open database: %s", database.lastError().text().toLatin1().data());
		return 1;
	}
	// Attach custom functions and tokenizers
	QVariant handler = database.driver()->handle();
	if (handler.isValid() && !qstrcmp(handler.typeName(), "sqlite3*")) {
		sqlite3 *sqliteHandler = *static_cast<sqlite3 **>(handler.data());
		// TODO Move into dedicated open function? Since it cannot use
		// the sqlite3_auto_extension
		register_all_tokenizers(sqliteHandler);
	}

	database.transaction();
	create_tables();

	// Prepare the queries
	Kanji::initializeQueries(database);

	// Parse kanjidic2
	QFile file("3rdparty/kanjidic2.xml");
	if (!file.open(QFile::ReadOnly | QFile::Text)) return 1;
	QXmlStreamReader reader(&file);
	if (process_kanjidic2(reader)) {
		return 1;
	}
	file.close();
	
	// Parse and insert KanjiVG data
	// TODO do not forget to add kanjis not referenced in kanjidic2!
	file.setFileName("3rdparty/kanjivg.xml");
	if (!file.open(QFile::ReadOnly | QFile::Text)) return 1;
	reader.setDevice(&file);
	if (process_kanjivg(reader)) {
		return 1;
	}
	file.close();

	// Insert JLPT levels
	Kanji::updateJLPTLevels("src/core/kanjidic2/jlpt-level1.txt", 1);
	Kanji::updateJLPTLevels("src/core/kanjidic2/jlpt-level2.txt", 2);
	Kanji::updateJLPTLevels("src/core/kanjidic2/jlpt-level3.txt", 3);
	Kanji::updateJLPTLevels("src/core/kanjidic2/jlpt-level4.txt", 4);
		
	// Create indexes
	create_indexes();
	
	// Commit everything
	database.commit();
	qDebug() << "Commited\n";
	return 0;
}
