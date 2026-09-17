#!/usr/bin/env python3
"""Testy rozvrhu a ukolu bez site: python3 -m unittest infra/school/test_feed.py

Tvar odpovedi kopiruje to, co o API Skoly OnLine rikaji neoficialni zdroje
(viz feed.py); jmena a predmety jsou vymyslene.
"""
import http.cookiejar
import json
import os
import random
import stat
import sys
import tempfile
import time
import unittest
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import feed  # noqa: E402
import serve  # noqa: E402


def hour(hour_id, day, begin, end, abbrev, name, caption, *, room="3.B",
         hour_type=("ROZVRH", "Rozvrh"), hour_kind=("", "", "")):
    return {
        "scheduledHourId": hour_id,
        "beginTime": f"{day}T{begin}:00",
        "endTime": f"{day}T{end}:00",
        "hourType": {"id": hour_type[0], "description": hour_type[1]},
        "hourKind": {"id": hour_kind[0], "name": hour_kind[1], "description": hour_kind[2]},
        "subject": {"id": "C1", "abbrev": abbrev, "name": name} if name else None,
        "groups": [{"name": "III.B", "kind": "c"}],
        "rooms": [{"abbrev": room, "name": f"{room} - 106"}],
        "teachers": [{"displayName": "Mgr. Jana Nováková"}],
        "detailHours": [{"name": caption, "order": 1}],
    }


TIMETABLE = {"days": [
    {"date": "2026-09-15T00:00:00", "schedules": [
        hour("C2", "2026-09-15", "08:55", "09:40", "M", "Matematika", "2."),
        hour("C1", "2026-09-15", "08:00", "08:45", "Čj", "Český jazyk", "1."),
        # Skutecna skola posila poradi bez tecky (overeno 15. 9. 2026).
        hour("C3", "2026-09-15", "10:00", "10:45", "Tv", "Tělesná výchova", "3", room="TV"),
    ]},
    {"date": "2026-09-16T00:00:00", "schedules": [
        hour("C4", "2026-09-16", "08:00", "08:45", "Aj", "Anglický jazyk", "1.",
             hour_type=("SUPLOVANA", "Suplovaná")),
        hour("C5", "2026-09-16", "08:00", "08:45", "Aj", "Anglický jazyk", "1.",
             hour_type=("SUPLOVANI", "Suplování"),
             hour_kind=("SUPLOVANI", "Suplování", "Suplování předmětu")),
        hour("C6", "2026-09-16", "08:55", "09:40", "Hv", "Hudební výchova", "2.",
             hour_type=("ODPADLA", "Odpadlá hodina")),
        hour("C7", "2026-09-16", "10:00", "11:35", "", "", "3.",
             hour_type=("AKCE", "Školní akce")),
    ]},
    {"date": "2026-09-19T00:00:00", "schedules": []},
]}

HOMEWORK = {"homeworks": [
    # Tvar podle aplikace resol.
    {"id": "U2", "name": "Pracovní sešit str. 12", "dateEnd": "2026-09-16T00:00:00",
     "subject": {"name": "Matematika"}, "teacher": {"displayName": "Mgr. X"},
     "content": "<p>Cvičení 1&ndash;3</p>", "attachments": []},
    # Tvar podle dokumentace Libre-SkolaOnline.
    {"id": "U1", "topic": "", "dateTo": "2026-09-15T00:00:00",
     "detailedDescription": "<p>Přečíst <b>kapitolu</b><br>o&nbsp;Praze</p>",
     "subject": {"name": "Český jazyk"}},
    {"id": "U3", "name": "Starý úkol", "dateEnd": "2026-09-10T00:00:00", "subject": {"name": "Prvouka"}},
    {"id": "U4", "name": "Hotový", "dateEnd": "2026-09-17T00:00:00", "isDone": True},
    {"id": "U5", "name": "Bez termínu"},
    {"id": "U6", "name": "Za měsíc", "dateEnd": "2026-10-20T00:00:00"},
]}

MESSAGES = {"messages": [
    {"id": "Z1", "title": "Třídní schůzky", "sentDate": "2026-09-14T18:02:11.5", "read": False,
     "text": "<p>Soukromý obsah</p>", "sender": {"name": "Mgr. Jana Nováková", "type": "employee"}},
    {"id": "Z2", "title": "Výlet", "sentDate": "2026-09-15T07:30:00", "read": True,
     "sender": {"name": "Mgr. Petr Dvořák"}},
    {"id": "Z3", "title": "", "sentDate": "2026-09-15T09:00:00", "read": False,
     "sender": {"name": "PaedDr. Eva Malá, Ph.D."}},
    # Stara neprectena zprava: na hodinach by visela porad.
    {"id": "Z4", "title": "Vánoční besídka", "sentDate": "2025-12-08T14:37:03", "read": False,
     "sender": {"name": "Jan Svoboda"}},
    # Bez priznaku read se zprava za neprectenou nepovazuje.
    {"id": "Z5", "title": "Bez příznaku", "sentDate": "2026-09-15T10:00:00",
     "sender": {"name": "Jan Svoboda"}},
]}

MARKS = {
    "marks": [
        {"id": "K1", "subjectId": "S1", "markText": "1", "theme": "Násobilka",
         "markDate": "2026-09-14T00:00:00", "weight": 1},
        {"id": "K2", "subjectId": "S2", "markText": "Ok", "theme": "Diktát",
         "markDate": "2026-08-20T00:00:00", "weight": 0.2},
        {"id": "K3", "subjectId": "S9", "markText": "2-", "theme": "",
         "markDate": "2026-09-15T00:00:00"},
        {"id": "K4", "subjectId": "S1", "markText": "", "markDate": "2026-09-15T00:00:00"},
    ],
    "subjects": [{"id": "S1", "abbrev": "M", "name": "Matematika"},
                 {"id": "S2", "abbrev": "Čj", "name": "Český jazyk"}],
}


def at(text):
    return datetime.fromisoformat(text).replace(tzinfo=feed.TZ)


def snapshot():
    return {
        "generated": "2026-09-15T07:00:00+02:00",
        "student": {"name": "Adam"},
        "lessons": feed.normalize_timetable(TIMETABLE),
        "homework": feed.normalize_homework(HOMEWORK),
    }


class TimetableTest(unittest.TestCase):
    def test_sorted_and_trimmed_to_display_fields(self):
        lessons = feed.normalize_timetable(TIMETABLE)
        first = lessons[0]
        self.assertEqual([lesson["hour"] for lesson in lessons[:3]], ["1.", "2.", "3."])
        self.assertEqual(first["subject"], "Český jazyk")
        self.assertEqual(first["abbrev"], "Čj")
        self.assertNotIn("room", first)
        self.assertEqual(first["state"], feed.STATE_NORMAL)
        self.assertEqual(first["note"], "")
        self.assertNotIn("teacher", first)

    def test_substitution_replaces_original(self):
        tomorrow = [lesson for lesson in feed.normalize_timetable(TIMETABLE)
                    if lesson["start"].startswith("2026-09-16")]
        english = [lesson for lesson in tomorrow if lesson["abbrev"] == "Aj"]
        self.assertEqual(len(english), 1)
        self.assertEqual(english[0]["state"], feed.STATE_CHANGED)
        self.assertEqual(english[0]["note"], "Suplování")

    def test_canceled_and_event(self):
        tomorrow = [lesson for lesson in feed.normalize_timetable(TIMETABLE)
                    if lesson["start"].startswith("2026-09-16")]
        music = next(lesson for lesson in tomorrow if lesson["abbrev"] == "Hv")
        self.assertEqual(music["state"], feed.STATE_CANCELED)
        event = tomorrow[-1]
        self.assertEqual(event["subject"], "Školní akce")
        self.assertEqual(event["state"], feed.STATE_CHANGED)

    def test_garbage_is_ignored(self):
        self.assertEqual(feed.normalize_timetable(None), [])
        self.assertEqual(feed.normalize_timetable({"days": [None, {"schedules": [1, {}]}]}), [])


class HomeworkTest(unittest.TestCase):
    def test_both_known_shapes(self):
        homework = feed.normalize_homework(HOMEWORK)
        self.assertEqual([item["id"] for item in homework], ["U3", "U1", "U2", "U6"])
        czech = homework[1]
        # Prazdny "topic" -> titulek z popisu, bez HTML a s dekodovanymi entitami.
        self.assertEqual(czech["title"], "Přečíst kapitolu o Praze")
        self.assertEqual(czech["due"], "2026-09-15")
        self.assertEqual(homework[2]["subject"], "Matematika")

    def test_plain_text_shortens_on_word(self):
        text = feed.plain_text("slovo " * 30, 20)
        self.assertTrue(text.endswith("…"))
        self.assertLessEqual(len(text), 20)
        self.assertNotIn("  ", text)


class MessagesAndMarksTest(unittest.TestCase):
    def test_only_unread_without_body(self):
        messages = feed.normalize_messages(MESSAGES)
        self.assertEqual([message["id"] for message in messages], ["Z3", "Z1", "Z4"])
        self.assertEqual(messages[1]["sender"], "Nováková")
        self.assertEqual(messages[1]["title"], "Třídní schůzky")
        self.assertEqual((messages[0]["sender"], messages[0]["title"]), ("Malá", "Zpráva"))
        self.assertNotIn("Soukromý", json.dumps(messages, ensure_ascii=False))

    def test_marks_with_subjects(self):
        marks = feed.normalize_marks(MARKS)
        self.assertEqual([mark["id"] for mark in marks], ["K3", "K1", "K2"])
        self.assertEqual((marks[1]["abbrev"], marks[1]["mark"], marks[1]["theme"]), ("M", "1", "Násobilka"))
        # Predmet mimo ciselnik: znamka zustane, jen bez nazvu.
        self.assertEqual((marks[0]["subject"], marks[0]["mark"]), ("", "2-"))
        self.assertEqual(feed.normalize_marks(None), [])

    def test_render_windows_labels_and_counts(self):
        snap = {**snapshot(), "messages": feed.normalize_messages(MESSAGES),
                "marks": feed.normalize_marks(MARKS)}
        body = feed.render(snap, at("2026-09-15T12:00"))
        self.assertEqual(body["messageCount"], 2)
        self.assertEqual(body["messages"], [
            {"when": "DNES", "sender": "Malá", "title": "Zpráva"},
            {"when": "VČERA", "sender": "Nováková", "title": "Třídní schůzky"}])
        self.assertEqual(body["markCount"], 2)
        self.assertEqual([(mark["when"], mark["abbrev"], mark["mark"]) for mark in body["marks"]],
                         [("DNES", "", "2-"), ("VČERA", "M", "1")])

    def test_render_without_extras_has_no_keys(self):
        body = feed.render(snapshot(), at("2026-09-15T12:00"))
        self.assertNotIn("messages", body)
        self.assertNotIn("marks", body)

    def test_counts_are_not_capped_by_rows(self):
        many = {"messages": [{"id": f"Z{index}", "title": "Info", "read": False,
                              "sentDate": f"2026-09-{index + 1:02d}T08:00:00"} for index in range(10)]}
        snap = {**snapshot(), "messages": feed.normalize_messages(many)}
        body = feed.render(snap, at("2026-09-15T12:00"))
        self.assertEqual((body["messageCount"], len(body["messages"])), (10, feed.MAX_MESSAGES))


BOARD = """<div class='menu'><div class='modul_nazev'>Nástěnka</div></div>
<div class='podnadpis flex space-between'>
 <div class='left bold'>Střevní&nbsp;problémy </div>
 <div class='right bold'>16.9.2026 11:01:13</div>
</div>
<div class='container' id='612'><div class='nastenka_obsah'>
 <div class='section padding_informations justify'>
 Vážení rodiče,
děti v budově mají střevní problémy.<br>Děkujeme
 </div>
 <div class='media justified dashed_top dashed_bottom'></div>
</div></div>
<div class='podnadpis flex space-between'>
 <div class='left bold'>Bez data</div><div class='right bold'></div>
</div>
<div class='podnadpis flex space-between'>
 <div class='left bold'>Čaj o páté</div>
 <div class='right bold'>31.8.2026 15:29:51</div>
</div>
<div class='container'><div class='nastenka_obsah'>
 <div class='section'>Dobrý den, zvu Vás na &quot;Čaj o páté&quot;.</div>
</div></div>
<div class='podnadpis flex space-between'>
 <div class='left bold'>Odstávka</div>
 <div class='right bold'>17.9.2026 07:04:41</div>
</div>
<div class='container'><div class='nastenka_obsah'><div class='section'></div></div></div>
"""
LOGIN_PAGE = "<form action='https://nasems.cz/' method='post'><input type='password' class='pw' name='password' value='' /></form>"



def session_cookie():
    return http.cookiejar.Cookie(0, "PHPSESSID", "x", None, False, "nasems.test", False, False,
                                 "/", False, False, None, False, None, None, {})


class NoticesTest(unittest.TestCase):
    def test_board_titles_dates_and_text_without_greeting(self):
        notices = feed.normalize_notices(BOARD)
        self.assertEqual(notices, [
            {"posted": "2026-09-17T07:04", "title": "Odstávka", "text": ""},
            {"posted": "2026-09-16T11:01", "title": "Střevní problémy",
             "text": "děti v budově mají střevní problémy. Děkujeme"},
            {"posted": "2026-08-31T15:29", "title": "Čaj o páté", "text": "zvu Vás na \"Čaj o páté\"."},
        ])
        self.assertEqual(feed.normalize_notices(""), [])
        self.assertEqual(feed.normalize_notices(LOGIN_PAGE), [])

    def test_login_page_is_recognised(self):
        self.assertTrue(feed.is_nasems_login_page(LOGIN_PAGE))
        self.assertFalse(feed.is_nasems_login_page(BOARD))

    def test_render_two_week_window(self):
        snap = {**snapshot(), "notices": feed.normalize_notices(BOARD)}
        body = feed.render(snap, at("2026-09-17T12:00"))
        self.assertEqual(body["noticeCount"], 2)
        self.assertEqual([(notice["when"], notice["title"]) for notice in body["notices"]],
                         [("DNES", "Odstávka"), ("VČERA", "Střevní problémy")])
        self.assertNotIn("notices", feed.render(snapshot(), at("2026-09-17T12:00")))

    def test_client_signs_in_again_when_session_expired(self):
        class Fake(serve.NasemsClient):
            def __init__(self, password):
                super().__init__("rodic", password, "https://nasems.test")
                self.requests = []
                self.signed_in = False

            def _open(self, url, data=None):
                self.requests.append("POST" if data else "GET")
                if data:
                    self.signed_in = b"password=dobre" in data
                    return ""
                return BOARD if self.signed_in else LOGIN_PAGE

        client = Fake("dobre")
        # Prvni stazeni bez cookie: rovnou prihlaseni, pak nastenka.
        client.board()
        self.assertEqual(client.requests, ["POST", "GET"])
        client.cookies.set_cookie(session_cookie())
        client.requests.clear()
        client.board()
        self.assertEqual(client.requests, ["GET"])
        # Vyprsela session: formular, prihlaseni, nastenka.
        client.signed_in = False
        client.requests.clear()
        client.board()
        self.assertEqual(client.requests, ["GET", "POST", "GET"])
        wrong = Fake("spatne")
        wrong.cookies.set_cookie(session_cookie())
        with self.assertRaises(serve.AuthError):
            wrong.board()
        self.assertEqual(wrong.requests, ["GET", "POST", "GET"])


class StudentTest(unittest.TestCase):
    def test_parent_account_lists_children(self):
        user = {"userType": "parent", "personID": "P1", "children": [
            {"id": "C10", "firstName": "Adam", "displayName": "Adam Novák", "className": "III.B"},
            {"id": "C11", "firstName": "Eva", "displayName": "Eva Nováková", "className": "I.A"},
        ]}
        students = feed.students_from_user(user)
        self.assertEqual([student["id"] for student in students], ["C10", "C11"])
        self.assertEqual(feed.pick_student(students, "")["name"], "Adam")
        self.assertEqual(feed.pick_student(students, "eva")["id"], "C11")
        self.assertEqual(feed.pick_student(students, "C11")["name"], "Eva")
        self.assertIsNone(feed.pick_student(students, "Karel"))

    def test_parent_without_children_has_no_timetable(self):
        self.assertEqual(feed.students_from_user({"userType": "parent", "personID": "P1"}), [])

    def test_student_account(self):
        students = feed.students_from_user(
            {"personID": "S1", "fullName": "Adam Novák", "class": {"abbrev": "III.B"}})
        self.assertEqual(students, [{"id": "S1", "name": "Adam", "fullName": "Adam Novák",
                                     "class": "III.B"}])


class RenderTest(unittest.TestCase):
    def test_morning_shows_today_and_next_school_day(self):
        body = feed.render(snapshot(), at("2026-09-15T07:10"))
        self.assertEqual(body["student"], "Adam")
        today, tomorrow = body["days"]
        self.assertEqual((today["day"], today["weekday"], today["today"], today["end"]),
                         ("DNES", "ÚTERÝ", True, "10:45"))
        self.assertEqual([row["start"] for row in today["lessons"]], ["08:00", "08:55", "10:00"])
        self.assertEqual(today["lessons"][0], {
            "hour": "1.", "start": "08:00", "end": "08:45", "subject": "Český jazyk",
            "abbrev": "Čj", "note": "", "state": 0})
        self.assertEqual((tomorrow["day"], tomorrow["weekday"], tomorrow["today"]),
                         ("ZÍTRA", "STŘEDA", False))
        # Posledni hodina je skolni akce do 11:35; odpadla hodina konec neurcuje.
        self.assertEqual(tomorrow["end"], "11:35")

    def test_today_holds_until_grace_after_last_lesson(self):
        self.assertEqual(feed.render(snapshot(), at("2026-09-15T10:59"))["days"][0]["day"], "DNES")
        body = feed.render(snapshot(), at("2026-09-15T11:01"))
        # Po 16. 9. uz v rozvrhu zadny skolni den neni, takze zbyde jeden sloupec.
        self.assertEqual([day["day"] for day in body["days"]], ["ZÍTRA"])
        self.assertEqual(len(body["days"][0]["lessons"]), 3)

    def test_weekend_skips_to_school_days_without_weekday(self):
        body = feed.render(snapshot(), at("2026-09-13T12:00"))
        self.assertEqual([(day["day"], day["weekday"]) for day in body["days"]],
                         [("ÚT 15.9.", ""), ("ST 16.9.", "")])

    def test_friday_after_school_shows_monday_and_tuesday_skipping_holiday(self):
        # Overeny tvar skutecneho rozvrhu: pa 25. 9., po 28. 9. je statni svatek
        # (Skola OnLine pro nej hodiny neposila), ut 29. 9. a st 30. 9.
        days = {"2026-09-25": "12:20", "2026-09-29": "13:15", "2026-09-30": "12:20"}
        timetable = {"days": [{"date": f"{day}T00:00:00", "schedules": [
            hour(f"C{day}", day, "07:50", end, "M", "Matematika", "1")]}
            for day, end in days.items()]}
        snap = {"lessons": feed.normalize_timetable(timetable)}
        morning = feed.render(snap, at("2026-09-25T07:00"))["days"]
        self.assertEqual([(d["day"], d["end"]) for d in morning],
                         [("DNES", "12:20"), ("ÚT 29.9.", "13:15")])
        afternoon = feed.render(snap, at("2026-09-25T14:00"))["days"]
        self.assertEqual([d["day"] for d in afternoon], ["ÚT 29.9.", "ST 30.9."])
        self.assertFalse(any(d["today"] for d in afternoon))
        sunday = feed.render(snap, at("2026-09-27T20:00"))["days"]
        self.assertEqual([d["day"] for d in sunday], ["ÚT 29.9.", "ST 30.9."])
        monday = feed.render(snap, at("2026-09-28T20:00"))["days"]
        self.assertEqual([(d["day"], d["weekday"]) for d in monday],
                         [("ZÍTRA", "ÚTERÝ"), ("ST 30.9.", "")])

    def test_day_count_is_configurable(self):
        original = feed.DAY_COUNT
        try:
            feed.DAY_COUNT = 1
            self.assertEqual(len(feed.render(snapshot(), at("2026-09-15T07:10"))["days"]), 1)
        finally:
            feed.DAY_COUNT = original

    def test_holidays_have_no_days(self):
        self.assertEqual(feed.render(snapshot(), at("2026-09-17T12:00"))["days"], [])

    def test_canceled_whole_day_has_no_end(self):
        lessons = [lesson for lesson in feed.normalize_timetable(TIMETABLE)
                   if lesson["abbrev"] == "Hv"]
        body = feed.render({"lessons": lessons}, at("2026-09-16T07:00"))
        self.assertEqual((body["days"][0]["day"], body["days"][0]["end"]), ("DNES", ""))

    def test_homework_window_labels_and_abbrev_from_timetable(self):
        body = feed.render(snapshot(), at("2026-09-15T12:00"))
        self.assertEqual([(item["due"], item["abbrev"]) for item in body["homework"]],
                         [("DNES", "Čj"), ("ZÍTRA", "M")])
        # Po pulnoci je vcerejsi ukol pryc, i kdyz se nic nestahovalo.
        later = feed.render(snapshot(), at("2026-09-16T00:05"))
        self.assertEqual([item["due"] for item in later["homework"]], ["DNES"])

    def test_output_is_compact_json(self):
        body = json.dumps(feed.render(snapshot(), at("2026-09-15T07:10")), ensure_ascii=False)
        self.assertLess(len(body.encode()), 3072)


class PollerTest(unittest.TestCase):
    def test_auth_failure_backs_off_and_keeps_old_data(self):
        class Refusing:
            def user(self):
                raise serve.AuthError("HTTP 400")

        poller = serve.Poller(Refusing())
        poller.snapshot = snapshot()
        poller.fetched_at = time.time()
        delay = poller.poll_once()
        self.assertEqual(delay, serve.AUTH_BACKOFF_HOURS * 3600)
        current = poller.current()
        self.assertEqual(current["student"]["name"], "Adam")
        self.assertIn("odmitla", current["problem"])

    def test_stale_snapshot_is_not_served(self):
        poller = serve.Poller(None)
        poller.snapshot = snapshot()
        poller.fetched_at = time.time() - serve.MAX_AGE_HOURS * 3600 - 60
        self.assertIsNone(poller.current())

    def test_student_is_looked_up_once_and_again_after_failure(self):
        class Counting:
            users = timetables = 0
            fail = False

            def user(self):
                self.users += 1
                return {"personID": "S1", "fullName": "Adam Novák"}

            def timetable(self, student_id, *args):
                self.timetables += 1
                if self.fail:
                    raise serve.SchoolError("HTTP 403")
                self.last_student = student_id
                return TIMETABLE

            def homework(self, *args):
                return HOMEWORK

            def messages(self):
                return MESSAGES

            def marks(self, *args):
                return MARKS

        client = Counting()
        poller = serve.Poller(client)
        poller.poll_once()
        poller.poll_once()
        self.assertEqual((client.users, client.timetables), (1, 2))
        self.assertEqual(client.last_student, "S1")
        self.assertNotIn("id", feed.render(poller.current()))
        client.fail = True
        poller.poll_once()
        client.fail = False
        poller.poll_once()
        self.assertEqual(client.users, 2)

    def test_extras_follow_their_own_schedule_and_never_break_timetable(self):
        class Client:
            messages_calls = marks_calls = 0
            messages_fail = False

            def user(self):
                return {"personID": "S1", "fullName": "Adam Novák"}

            def timetable(self, *args):
                return TIMETABLE

            def homework(self, *args):
                return HOMEWORK

            def messages(self):
                self.messages_calls += 1
                if self.messages_fail:
                    raise serve.SchoolError("HTTP 500")
                return MESSAGES

            def marks(self, *args):
                self.marks_calls += 1
                raise serve.SchoolError("HTTP 403")

        class Nasems:
            calls = 0

            def board(self):
                self.calls += 1
                return BOARD

        client = Client()
        nasems = Nasems()
        poller = serve.Poller(client, nasems=nasems)
        # V noci se zpravy po prvnim stazeni neobnovuji; test nesmi zaviset na hodine.
        day_hours = serve.DAY_HOURS
        serve.DAY_HOURS = (0, 24)
        self.addCleanup(setattr, serve, "DAY_HOURS", day_hours)
        poller.poll_once()
        poller.poll_once()
        # Dve stazeni rozvrhu za sebou: zpravy i znamky jen jednou.
        self.assertEqual((client.messages_calls, client.marks_calls), (1, 1))
        self.assertEqual(nasems.calls, 1)
        self.assertEqual(len(poller.current()["notices"]), 3)
        current = poller.current()
        self.assertEqual(len(current["messages"]), 3)
        # Znamky selhaly, rozvrh i zpravy zustaly.
        self.assertNotIn("marks", current)
        self.assertEqual(len(current["lessons"]), 6)
        # Po hodine se zpravy zkusi znovu; chyba necha starsi seznam.
        client.messages_fail = True
        poller.extras_tried["messages"] -= serve.MESSAGES_POLL_MINUTES * 60
        poller.poll_once()
        self.assertEqual(client.messages_calls, 2)
        self.assertEqual(len(poller.current()["messages"]), 3)
        # V noci uz stazene zpravy cekaji do rana.
        serve.DAY_HOURS = (0, 0)
        poller.extras_tried["messages"] -= serve.MESSAGES_POLL_MINUTES * 60
        poller.poll_once()
        self.assertEqual(client.messages_calls, 2)

    def test_errors_back_off_and_honour_retry_after(self):
        class Down:
            retry_after = None

            def user(self):
                raise serve.SchoolError("HTTP 503", self.retry_after)

        client = Down()
        poller = serve.Poller(client)
        delays = [poller.poll_once() for _ in range(6)]
        self.assertEqual(delays, [600, 1200, 2400, 4800, 7200, 7200])
        client.retry_after = 3 * 3600
        self.assertEqual(poller.poll_once(), 3 * 3600)
        client.retry_after = 99 * 3600
        self.assertEqual(poller.poll_once(), serve.AUTH_BACKOFF_HOURS * 3600)

    def _stored_poller(self, directory):
        """Poller s uspesnym stazenim ulozenym na disk."""
        class Client:
            def user(self):
                return {"personID": "S1", "fullName": "Adam Novák"}

            def timetable(self, *args):
                return TIMETABLE

            def homework(self, *args):
                return HOMEWORK

            def messages(self):
                return MESSAGES

            def marks(self, *args):
                return MARKS

        path = os.path.join(directory, "state.json")
        poller = serve.Poller(Client(), path)
        poller.poll_once()
        return poller, path

    def test_state_survives_restart_privately(self):
        with tempfile.TemporaryDirectory() as directory:
            before, path = self._stored_poller(directory)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
            self.assertFalse(os.path.exists(path + ".tmp"))
            after = serve.Poller(None, path)
            self.assertTrue(after.load_state())
            self.assertEqual(after.current()["lessons"], before.current()["lessons"])
            self.assertEqual(len(after.current()["messages"]), 3)
            self.assertEqual(after.student["id"], "S1")
            self.assertEqual(after.extras_tried.keys(), before.extras_tried.keys())
            # Cerstva data: prvni dotaz az po intervalu, ne hned po startu.
            self.assertGreater(after.initial_wait(), 0)

    def test_state_is_ignored_when_old_broken_or_foreign(self):
        with tempfile.TemporaryDirectory() as directory:
            _, path = self._stored_poller(directory)
            with open(path, encoding="utf-8") as handle:
                good = json.load(handle)

            def load(state):
                with open(path, "w", encoding="utf-8") as handle:
                    handle.write(state if isinstance(state, str) else json.dumps(state))
                poller = serve.Poller(None, path)
                return poller, poller.load_state()

            self.assertFalse(load("{nedopsany")[1])
            self.assertFalse(load({**good, "account": "jiny.rodic"})[1])
            self.assertFalse(load({**good, "selector": "Eva"})[1])
            self.assertFalse(load({**good, "snapshot": {"lessons": "x"}})[1])
            broken = json.loads(json.dumps(good))
            broken["snapshot"]["lessons"] = [{"start": "nesmysl"}]
            self.assertFalse(load(broken)[1])
            # Stary soubor se nacte, ale data se nevydaji a dotaz jde hned.
            old = {**good, "fetched_at": time.time() - serve.MAX_AGE_HOURS * 3600 - 60}
            poller, loaded = load(old)
            self.assertTrue(loaded)
            self.assertIsNone(poller.current())
            self.assertEqual(poller.initial_wait(), 0)
            # Cas z budoucnosti se srovna na ted.
            poller, _ = load({**good, "fetched_at": time.time() + 10 * 86400})
            self.assertLessEqual(poller.fetched_at, time.time())
            self.assertIsNone(serve.Poller(None, os.path.join(directory, "chybi.json")).current())

    def test_disabled_extras_are_not_served_from_disk(self):
        with tempfile.TemporaryDirectory() as directory:
            _, path = self._stored_poller(directory)
            enabled = serve.MESSAGES_ENABLED
            serve.MESSAGES_ENABLED = False
            self.addCleanup(setattr, serve, "MESSAGES_ENABLED", enabled)
            poller = serve.Poller(None, path)
            self.assertTrue(poller.load_state())
            self.assertNotIn("messages", poller.current())
            self.assertIn("marks", poller.current())

    def test_jitter_stays_within_bounds(self):
        rng = random.Random(1)
        delays = [serve.jittered(1200, 10, rng) for _ in range(500)]
        self.assertTrue(all(1080 <= delay <= 1320 for delay in delays))
        # Opravdu se to meni, ne jen jedna hodnota.
        self.assertGreater(max(delays) - min(delays), 200)
        self.assertEqual(serve.jittered(1200, 0, rng), 1200)
        # Nesmysl v promenne prostredi nesmi vest k zapornemu cekani.
        self.assertTrue(all(serve.jittered(1200, 500, rng) > 0 for _ in range(100)))

    def test_quiet_hours_cover_the_night(self):
        rng = random.Random(2)
        hour = 3600
        spread = serve.QUIET_SPREAD_MINUTES * 60
        self.assertEqual(serve.quiet_wait(at("2026-09-15T21:59"), "22-5", rng), 0)
        self.assertEqual(serve.quiet_wait(at("2026-09-15T05:00"), "22-5", rng), 0)
        # Vychozi ticho: ve 22:00 zacina a v 5:00 konci.
        self.assertEqual(serve.QUIET_HOURS, "22-5")
        late = serve.quiet_wait(at("2026-09-15T23:30"), "22-5", rng)
        self.assertTrue(5.5 * hour <= late <= 5.5 * hour + spread)
        early = serve.quiet_wait(at("2026-09-16T04:59"), "22-5", rng)
        self.assertTrue(60 <= early <= 60 + spread)
        # Nova hodina ticha: ve 22:30 se ceka pres celou noc.
        just_quiet = serve.quiet_wait(at("2026-09-15T22:30"), "22-5", rng)
        self.assertTrue(6.5 * hour <= just_quiet <= 6.5 * hour + spread)
        # Okno ve dne bez prechodu pres pulnoc a vypnute ticho.
        self.assertTrue(serve.quiet_wait(at("2026-09-15T13:00"), "12-14", rng) >= hour)
        self.assertEqual(serve.quiet_wait(at("2026-09-15T01:00"), "", rng), 0)
        self.assertEqual(serve.quiet_wait(at("2026-09-15T01:00"), "5-5", rng), 0)

    def test_quiet_hours_across_daylight_saving_change(self):
        # 25. 10. 2026 ve 3:00 se hodiny vraci na 2:00: od 1:00 do 5:00 je pet
        # skutecnych hodin, ne ctyri.
        wait = serve.quiet_wait(at("2026-10-25T01:00"), "22-5", random.Random(3))
        self.assertTrue(5 * 3600 <= wait <= 5 * 3600 + serve.QUIET_SPREAD_MINUTES * 60)

    def test_poll_interval_follows_school_days(self):
        snap = snapshot()  # ut 15. 9. do 10:45, st 16. 9. do 11:35
        day, idle = serve.DAY_POLL_MINUTES, serve.IDLE_POLL_MINUTES
        # Pondeli: dopoledne obcas, od poledne pred skolnim dnem casto.
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-14T09:00")), idle)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-14T18:00")), day)
        # Utery do konce vyucovani, pak az od poledne kvuli zitrku.
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T10:00")), day)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T11:00")), idle)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T13:00")), day)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T23:00")), serve.NIGHT_POLL_MINUTES)
        # Pri tristi minutach je rano stejne jako zbytek dne.
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T05:05")), day)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T02:00")), serve.NIGHT_POLL_MINUTES)
        # Streda po skole: zitra se neuci.
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-16T14:00")), idle)
        self.assertEqual(serve.poll_minutes({"lessons": []}, at("2026-07-15T10:00")),
                         serve.HOLIDAY_POLL_MINUTES)

    def test_morning_wait_does_not_overshoot_the_day_window(self):
        # Kratsi denni odstup (nez vychozi tri hodiny) nesmi rano prespat
        # zacatek dne: v 5:05 se dalsi dotaz vejde do 6:00.
        snap = snapshot()
        for name, value in (("DAY_POLL_MINUTES", 20), ("NIGHT_POLL_MINUTES", 120)):
            self.addCleanup(setattr, serve, name, getattr(serve, name))
            setattr(serve, name, value)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T05:05")), 55)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T05:54")), 20)
        self.assertEqual(serve.poll_minutes(snap, at("2026-09-15T02:00")), 120)

    def test_unexpected_shape_does_not_kill_thread(self):
        class Broken:
            def user(self):
                return {"personID": "S1", "fullName": "Adam"}

            def timetable(self, *args):
                return {"days": "nesmysl"}

            def homework(self, *args):
                raise KeyError("homeworks")

        poller = serve.Poller(Broken())
        self.assertEqual(poller.poll_once(), serve.RETRY_MINUTES * 60)
        self.assertIsNone(poller.current())


if __name__ == "__main__":
    unittest.main()
