import json
import os
import re
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse

# Import the parser logic
from parser import parse_linkedin_report

TRACKER_FILE = "/Users/amgad/Desktop/Ai/job_tracker/job_applications.md"
REPORT_FILE = "/Users/amgad/Desktop/Ai/job_tracker/linkedin_jobs_montreal_southshore.md"

class TrackerHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        
        if parsed.path == '/':
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
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
            self.end_headers()
            data = self.read_tracker()
            self.wfile.write(json.dumps(data).encode('utf-8'))
        
        elif parsed.path == '/api/pending-jobs':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
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
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length)
            data = json.loads(body.decode('utf-8'))
            
            self.update_tracker(data.get('application'), data.get('index', -1))
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
    
    def do_DELETE(self):
        if self.path == '/api/applications':
            length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(length)
            data = json.loads(body.decode('utf-8'))
            
            self.delete_application(data.get('index'))
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(b'{"status":"ok"}')
    
    def read_tracker(self):
        """Read applications from markdown file"""
        if not os.path.exists(TRACKER_FILE):
            return {'applications': [], 'lastUpdated': None}
        
        with open(TRACKER_FILE, 'r', encoding='utf-8') as f:
            content = f.read()
        
        apps = []
        lines = content.split('\n')
        in_table = False
        
        for line in lines:
            if line.startswith('| # |') and 'Company' in line:
                in_table = True
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
                    except:
                        pass
        
        last_updated = None
        for line in lines:
            if 'Last Updated:' in line:
                last_updated = line.split('Last Updated:')[1].strip()
        
        return {'applications': apps, 'lastUpdated': last_updated}
    
    def update_tracker(self, app, index):
        """Add or update an application"""
        apps = self.read_tracker()['applications']
        
        if index >= 0 and index < len(apps):
            apps[index] = app
        else:
            apps.append(app)
        
        self.write_tracker(apps)
    
    def delete_application(self, index):
        """Delete an application"""
        apps = self.read_tracker()['applications']
        
        if 0 <= index < len(apps):
            apps.pop(index)
            self.write_tracker(apps)
    
    def write_tracker(self, apps):
        """Write applications to markdown file"""
        today = datetime.now().strftime('%Y-%m-%d')
        
        stats = {
            'total': len(apps),
            'Applied': len([a for a in apps if a.get('status') == 'Applied']),
            'Phone Screen': len([a for a in apps if a.get('status') == 'Phone Screen']),
            'Interview': len([a for a in apps if a.get('status') == 'Interview']),
            'Offer': len([a for a in apps if a.get('status') == 'Offer']),
            'Rejected': len([a for a in apps if a.get('status') == 'Rejected']),
            'Withdrawn': len([a for a in apps if a.get('status') == 'Withdrawn'])
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
        lines.append(f"| Phone Screen | {stats['Phone Screen']} |")
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
        
        for idx, app in enumerate(apps):
            contact = app.get('contact', '')
            follow_up = app.get('followUp', '')
            notes = app.get('notes', '')
            source = app.get('source', 'Manual')
            row = f"| {idx + 1} | {app.get('date', '')} | {app.get('company', '')} | {app.get('title', '')} | {source} | {app.get('status', '')} | {contact} | {follow_up} | {notes} |"
            lines.append(row)
        
        if not apps:
            lines.append("| *No applications tracked yet* |")
        
        with open(TRACKER_FILE, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
    
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