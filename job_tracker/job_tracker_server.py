import json
import os
import re
import shutil
import traceback
from enum import Enum
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse

# Import the parser logic
from parser import parse_linkedin_report

TRACKER_FILE = "/Users/amgad/Desktop/Ai/job_tracker/job_applications.md"
REPORT_FILE = "/Users/amgad/Desktop/Ai/job_tracker/linkedin_jobs_montreal_southshore.md"
BACKUP_DIR = "/Users/amgad/Desktop/Ai/job_tracker/backup/tracker_snapshots"
ERROR_LOG = "/Users/amgad/Desktop/Ai/job_tracker/server.log"
DELETED_LOG = "/Users/amgad/Desktop/Ai/job_tracker/deleted_applications.md"
CHANGELOG_FILE = "/Users/amgad/Desktop/Ai/job_tracker/applications_changelog.md"
MAX_BACKUPS = 200

class ApplicationStatus(Enum):
    APPLIED = "Applied"
    PRE_INTERVIEW = "Pre-Interview"
    WAIT_FOR_RESPONSE = "Wait For Response"
    PRE_TECHNICAL_TEST = "Pre-Technical-Test"
    POST_TECHNICAL_TEST = "Post-Technical-Test"
    INTERVIEW = "Interview"
    OFFER = "Offer"
    REJECTED = "Rejected"
    WITHDRAWN = "Withdrawn"


class TrackerHandler(BaseHTTPRequestHandler):
    def add_security_headers(self):
        self.send_header('Content-Security-Policy', "frame-ancestors 'none'")
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('X-Frame-Options', 'DENY')

    def send_json_error(self, code, message):
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.add_security_headers()
        self.end_headers()
        self.wfile.write(json.dumps({'error': message}).encode('utf-8'))

    def do_GET(self):
        parsed = urlparse(self.path)
        
        if parsed.path == '/':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.add_security_headers()
            self.end_headers()
            # Serve the external HTML file
            try:
                with open("index.html", 'r', encoding='utf-8') as f:
                    self.wfile.write(f.read().encode('utf-8'))
            except FileNotFoundError:
                self.wfile.write(b"index.html not found. Please ensure it is in the same directory.")
        
        elif parsed.path == '/api/applications':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.add_security_headers()
            self.end_headers()
            data = self.read_tracker()
            self.wfile.write(json.dumps(data).encode('utf-8'))
        
        elif parsed.path == '/api/pending-jobs':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.add_security_headers()
            self.end_headers()
            
            # Filter out companies we already applied to
            applied_companies = set()
            if os.path.exists(TRACKER_FILE):
                with open(TRACKER_FILE, 'r', encoding='utf-8') as f:
                    tracker_content = f.read()
                for line in tracker_content.split('\n'):
                    if line.startswith('|') and 'Applied' in line:
                        parts = [p.strip() for p in line.split('|')]
                        if len(parts) > 3 and parts[3]:
                            applied_companies.add(parts[3].lower())
            
            jobs, report_date = parse_linkedin_report(REPORT_FILE)
            filtered_jobs = [j for j in jobs if j['company'].lower() not in applied_companies]

            self.wfile.write(json.dumps({
                'jobs': filtered_jobs,
                'count': len(filtered_jobs),
                'date': datetime.now().strftime('%Y-%m-%d'),
                'report_date': report_date
            }).encode('utf-8'))
        
        else:
            self.send_response(404)
            self.end_headers()
    
    def do_POST(self):
        if self.path == '/api/applications':
            try:
                length = int(self.headers.get('Content-Length', 0))
                if length <= 0 or length > 1_000_000:
                    return self.send_json_error(400, 'Invalid request body')
                body = self.rfile.read(length)
                data = json.loads(body.decode('utf-8'))
            except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
                return self.send_json_error(400, 'Malformed JSON body')

            app = data.get('application')
            index = data.get('index', -1)

            if not isinstance(app, dict):
                return self.send_json_error(400, 'application must be an object')
            if not isinstance(index, int):
                return self.send_json_error(400, 'index must be an integer')

            company = str(app.get('company', '')).strip()
            title = str(app.get('title', '')).strip()
            if not company or not title:
                return self.send_json_error(400, 'company and title are required')

            valid_statuses = {s.value for s in ApplicationStatus}
            status = app.get('status', ApplicationStatus.APPLIED.value)
            if status not in valid_statuses:
                return self.send_json_error(400, f'status must be one of {sorted(valid_statuses)}')

            # Normalize/sanitize string fields to plain strings, trim length to keep the
            # markdown table well-formed (strip pipe/newline chars that would break rows)
            def clean(v, max_len=500):
                return str(v or '').replace('|', '/').replace('\n', ' ').strip()[:max_len]

            safe_app = {
                'date': clean(app.get('date'), 20),
                'company': clean(company, 200),
                'title': clean(title, 200),
                'source': clean(app.get('source'), 500),
                'status': status,
                'contact': clean(app.get('contact'), 200),
                'followUp': clean(app.get('followUp'), 20),
                'notes': clean(app.get('notes'), 1000),
            }

            try:
                self.update_tracker(safe_app, index)
            except Exception as e:
                self.log_error(e)
                return self.send_json_error(500, f'Save failed, nothing was changed: {e}')

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.add_security_headers()
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
        else:
            self.send_json_error(404, 'Not found')

    def do_DELETE(self):
        if self.path == '/api/applications':
            try:
                length = int(self.headers.get('Content-Length', 0))
                if length <= 0 or length > 10_000:
                    return self.send_json_error(400, 'Invalid request body')
                body = self.rfile.read(length)
                data = json.loads(body.decode('utf-8'))
            except (json.JSONDecodeError, ValueError, UnicodeDecodeError):
                return self.send_json_error(400, 'Malformed JSON body')

            index = data.get('index')
            if not isinstance(index, int):
                return self.send_json_error(400, 'index must be an integer')

            apps_count = len(self.read_tracker()['applications'])
            if not (0 <= index < apps_count):
                return self.send_json_error(400, 'index out of range')

            try:
                self.delete_application(index)
            except Exception as e:
                self.log_error(e)
                return self.send_json_error(500, f'Delete failed, nothing was changed: {e}')

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.add_security_headers()
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
        else:
            self.send_json_error(404, 'Not found')
    
    def log_error(self, exc):
        """Append a traceback to server.log without ever raising, so a logging
        failure can't mask the original error or crash the request."""
        try:
            with open(ERROR_LOG, 'a', encoding='utf-8') as f:
                f.write(f"\n[{datetime.now().isoformat()}] {type(exc).__name__}: {exc}\n")
                f.write(traceback.format_exc())
                f.write("\n")
        except Exception:
            pass

    def read_tracker(self):
        """Read applications from markdown file"""
        if not os.path.exists(TRACKER_FILE):
            return {'applications': [], 'activeApplications': [], 'rejectedApplications': [], 'lastUpdated': None}
        
        with open(TRACKER_FILE, 'r', encoding='utf-8') as f:
            content = f.read()

        apps = self._parse_tracker_content(content)

        # Separate active and rejected applications
        active_apps = [a for a in apps if a.get('status') not in [ApplicationStatus.REJECTED.value, ApplicationStatus.WITHDRAWN.value]]
        rejected_apps = [a for a in apps if a.get('status') == ApplicationStatus.REJECTED.value]
        
        last_updated = None
        for line in content.split('\n'):
            if 'Last Updated:' in line:
                last_updated = line.split('Last Updated:')[1].strip()
        
        return {
            'applications': apps, 
            'activeApplications': active_apps, 
            'rejectedApplications': rejected_apps, 
            'lastUpdated': last_updated
        }
    
    def update_tracker(self, app, index):
        """Add or update an application. Reads a fresh copy right before writing
        so this never overwrites data from a concurrent change, and never
        touches the file unless the in-memory apps list was built successfully.

        NO-DATA-LOSS POLICY: if this is editing an existing row (index points
        at a real entry), the row's PREVIOUS values are permanently archived to
        CHANGELOG_FILE before being overwritten. If that archive write fails,
        the edit is aborted -- we never overwrite a row without a durable
        record of what it used to say.
        """
        apps = self.read_tracker()['applications']
        original_count = len(apps)

        if index >= 0 and index < len(apps):
            self._archive_row(CHANGELOG_FILE, apps[index], 'edited (previous values)')
            apps[index] = app
        else:
            apps.append(app)

        # Sanity check: an update/append must never result in fewer rows than
        # we started with. If it does, something upstream is broken -- bail
        # out instead of writing a smaller file.
        if len(apps) < original_count:
            raise RuntimeError(
                f'Refusing to write: app count would shrink from {original_count} to {len(apps)}'
            )

        self.write_tracker(apps)

    def delete_application(self, index):
        """Delete an application. Only ever removes exactly one row.

        NO-DATA-LOSS POLICY: nothing is ever truly deleted. The full row is
        permanently archived to DELETED_LOG *before* it is removed from the
        active tracker file. If the archive write fails, the row is NOT
        removed -- deletion only proceeds once we have durable proof the data
        survives elsewhere.
        """
        apps = self.read_tracker()['applications']
        original_count = len(apps)

        if not (0 <= index < len(apps)):
            raise RuntimeError(f'index {index} out of range for {len(apps)} applications')

        row_to_remove = apps[index]

        # Archive first. If this raises, we stop here and nothing is removed.
        self._archive_row(DELETED_LOG, row_to_remove, 'deleted')

        apps.pop(index)

        if len(apps) != original_count - 1:
            raise RuntimeError(
                f'Refusing to write: expected {original_count - 1} apps after delete, got {len(apps)}'
            )

        self.write_tracker(apps)

    def _archive_row(self, path, app, action):
        """Permanently append one application row to an append-only markdown
        log (DELETED_LOG or CHANGELOG_FILE), with a timestamp and the action
        that triggered the archive. This file is NEVER rewritten or truncated
        -- only ever appended to -- so it can serve as a permanent record even
        if the main tracker file is ever corrupted or mis-edited.

        Raises on failure (deliberately) so callers can abort the destructive
        operation that was about to happen, rather than proceeding without a
        backup of the data being lost.
        """
        is_new = not os.path.exists(path)
        with open(path, 'a', encoding='utf-8') as f:
            if is_new:
                f.write("# Archived Applications Log\n\n")
                f.write("Permanent, append-only record of every deleted or edited-over ")
                f.write("application row. Nothing in this file is ever removed.\n\n")
                f.write("| Timestamp | Action | Date | Company | Title | Source | Status | Contact | Follow-up | Notes |\n")
                f.write("|---|---|---|---|---|---|---|---|---|---|\n")
            timestamp = datetime.now().isoformat()
            row = (
                f"| {timestamp} | {action} | {app.get('date', '')} | {app.get('company', '')} | "
                f"{app.get('title', '')} | {app.get('source', '')} | {app.get('status', '')} | "
                f"{app.get('contact', '')} | {app.get('followUp', '')} | {app.get('notes', '')} |\n"
            )
            f.write(row)
            f.flush()
            os.fsync(f.fileno())
    
    def write_tracker(self, apps):
        """Write applications to markdown file.

        Safety measures (all required so a bug or crash here can never lose
        existing entries):
        1. Snapshot the current file into BACKUP_DIR before touching anything.
        2. Render the new content fully in memory first.
        3. Write it to a temp file in the same directory, flush + fsync it.
        4. Re-parse the temp file and confirm the row count matches what we
           intended to write. If it doesn't, abort -- the real file is
           untouched.
        5. Only then atomically replace the real file with os.replace(),
           which on the same filesystem is all-or-nothing (no partial file
           is ever visible).
        """
        self._backup_tracker_file()

        today = datetime.now().strftime('%Y-%m-%d')

        # Separate active applications from rejected/withdrawn
        active_apps = [a for a in apps if a.get('status') not in [ApplicationStatus.REJECTED.value, ApplicationStatus.WITHDRAWN.value]]
        rejected_apps = [a for a in apps if a.get('status') == ApplicationStatus.REJECTED.value]
        withdrawn_apps = [a for a in apps if a.get('status') == ApplicationStatus.WITHDRAWN.value]

        # Build status counts generically off the enum's own values, so a typo
        # in an attribute name can never crash this (that's what caused the
        # previous data-loss incident).
        status_counts = {s.value: 0 for s in ApplicationStatus}
        for a in apps:
            st = a.get('status')
            if st in status_counts:
                status_counts[st] += 1

        stats = {
            'total': len(apps),
            'Applied': status_counts[ApplicationStatus.APPLIED.value],
            'Pre-Interview': status_counts[ApplicationStatus.PRE_INTERVIEW.value],
            'Wait For Response': status_counts[ApplicationStatus.WAIT_FOR_RESPONSE.value],
            'Pre-Technical-Test': status_counts[ApplicationStatus.PRE_TECHNICAL_TEST.value],
            'Post-Technical-Test': status_counts[ApplicationStatus.POST_TECHNICAL_TEST.value],
            'Interview': status_counts[ApplicationStatus.INTERVIEW.value],
            'Offer': status_counts[ApplicationStatus.OFFER.value],
            'Rejected': len(rejected_apps),
            'Withdrawn': len(withdrawn_apps)
        }

        lines = []
        lines.append("# Job Application Tracker")
        lines.append("")
        lines.append(f"**Last Updated:** {today}")
        lines.append("")
        lines.append("## Summary")
        lines.append("")
        lines.append("| Status | Count |")
        lines.append("|--------|-------|")
        lines.append(f"| Applied | {stats['Applied']} |")
        lines.append(f"| Pre-Interview | {stats['Pre-Interview']} |")
        lines.append(f"| Wait For Response | {stats['Wait For Response']} |")
        lines.append(f"| Pre-Technical-Test | {stats['Pre-Technical-Test']} |")
        lines.append(f"| Post-Technical-Test | {stats['Post-Technical-Test']} |")
        lines.append(f"| Interview | {stats['Interview']} |")
        lines.append(f"| Offer | {stats['Offer']} |")
        lines.append(f"| Rejected | {stats['Rejected']} |")
        lines.append(f"| Withdrawn | {stats['Withdrawn']} |")
        lines.append(f"| **Total** | {stats['total']} |")
        lines.append("")
        lines.append("---")
        lines.append("")
        lines.append("## Applications")
        lines.append("")
        lines.append("| # | Date | Company | Title | Source | Status | Contact | Follow-up | Notes |")
        lines.append("|---|------|---------|-------|--------|--------|---------|-----------|-------|")

        for idx, app in enumerate(active_apps):
            contact = app.get('contact', '')
            follow_up = app.get('followUp', '')
            notes = app.get('notes', '')
            source = app.get('source', 'Manual')
            row = f"| {idx + 1} | {app.get('date', '')} | {app.get('company', '')} | {app.get('title', '')} | {source} | {app.get('status', '')} | {contact} | {follow_up} | {notes} |"
            lines.append(row)

        if not active_apps:
            lines.append("| *No active applications* | | | | | | | | |")

        # Add rejected applications section
        lines.append("")
        lines.append("---")
        lines.append("")
        lines.append("## Rejected Applications")
        lines.append("")
        lines.append("| # | Date | Company | Title | Source | Status | Contact | Follow-up | Notes |")
        lines.append("|---|------|---------|-------|--------|--------|---------|-----------|-------|")

        for idx, app in enumerate(rejected_apps):
            contact = app.get('contact', '')
            follow_up = app.get('followUp', '')
            notes = app.get('notes', '')
            source = app.get('source', 'Manual')
            row = f"| {idx + 1} | {app.get('date', '')} | {app.get('company', '')} | {app.get('title', '')} | {source} | {app.get('status', '')} | {contact} | {follow_up} | {notes} |"
            lines.append(row)

        if not rejected_apps:
            lines.append("| *No rejected applications* | | | | | | | | |")

        new_content = '\n'.join(lines)

        # Write to a temp file first, in the same directory (so os.replace stays
        # on one filesystem and is atomic), then fsync before replacing.
        tmp_path = TRACKER_FILE + '.tmp'
        with open(tmp_path, 'w', encoding='utf-8') as f:
            f.write(new_content)
            f.flush()
            os.fsync(f.fileno())

        # Verify: re-parse what we just wrote and confirm the row count matches
        # what we intended, and that withdrawn apps (which aren't rendered) are
        # accounted for. If anything is off, delete the temp file and abort
        # without ever touching the real tracker file.
        expected_total = len(active_apps) + len(rejected_apps) + len(withdrawn_apps)
        parsed_back = self._parse_tracker_content(new_content)
        parsed_visible = len(parsed_back)  # withdrawn apps are intentionally omitted from the file
        expected_visible = len(active_apps) + len(rejected_apps)
        if parsed_visible != expected_visible or expected_total != len(apps):
            try:
                os.remove(tmp_path)
            except OSError:
                pass
            raise RuntimeError(
                f'Write verification failed (expected {expected_visible} visible rows, '
                f'parsed back {parsed_visible}) -- aborted before touching the real file'
            )

        os.replace(tmp_path, TRACKER_FILE)

    def _backup_tracker_file(self):
        """Copy the current tracker file into BACKUP_DIR before every write,
        timestamped, so any bug can always be recovered from. Best-effort:
        a backup failure should not block the write of a first, sanity-checked
        file, but we do want it to fail loudly if the directory itself is
        unwritable, so problems get noticed rather than silently skipped."""
        if not os.path.exists(TRACKER_FILE):
            return
        os.makedirs(BACKUP_DIR, exist_ok=True)
        stamp = datetime.now().strftime('%Y%m%d_%H%M%S_%f')
        dest = os.path.join(BACKUP_DIR, f'job_applications_{stamp}.md')
        shutil.copy2(TRACKER_FILE, dest)
        self._prune_old_backups()

    def _prune_old_backups(self):
        """Keep only the most recent MAX_BACKUPS snapshots so the backup folder
        doesn't grow forever."""
        try:
            snapshots = sorted(
                (f for f in os.listdir(BACKUP_DIR) if f.startswith('job_applications_')),
            )
            excess = len(snapshots) - MAX_BACKUPS
            for name in snapshots[:max(excess, 0)]:
                os.remove(os.path.join(BACKUP_DIR, name))
        except OSError:
            pass

    def _parse_tracker_content(self, content):
        """Shared row-parsing logic used both by read_tracker() and by the
        post-write verification step, so verification checks exactly what
        read_tracker() will see."""
        apps = []
        lines = content.split('\n')
        in_table = False

        for line in lines:
            if line.startswith('| # |') and 'Company' in line:
                in_table = True
                continue
            if '## Rejected Applications' in line:
                in_table = False
                continue
            if in_table and line.startswith('|') and not line.startswith('| # |'):
                parts = [p.strip() for p in line.split('|')]
                cells = parts[1:-1]
                is_separator = cells and all(cell and set(cell) <= {'-', ':'} for cell in cells)
                if not is_separator and len(parts) >= 10 and parts[2] and parts[2] != 'Company' and not parts[2].startswith('*') and parts[3] != 'Company':
                    try:
                        app = {
                            'date': parts[2],
                            'company': parts[3],
                            'title': parts[4],
                            'source': parts[5],
                            'status': parts[6],
                            'contact': parts[7],
                            'followUp': parts[8],
                            'notes': parts[9] if len(parts) > 9 else ''
                        }
                        if app['company'] and app['title']:
                            apps.append(app)
                    except Exception:
                        pass
        return apps

    def log_message(self, format, *args):
        pass  # Suppress logging

def run_server(port=8766):
    server = HTTPServer(('localhost', port), TrackerHandler)
    process_id = os.getpid()
    print(f"🎯 Job Tracker running at http://localhost:{port}")
    print(f"   Process ID: {process_id}")
    print(f"   Tracker file: {TRACKER_FILE}")
    print(f"   Press Ctrl+C to stop")
    server.serve_forever()

if __name__ == '__main__':
    run_server()