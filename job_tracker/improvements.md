 Project Analysis: Job Application Tracker                                      
                                                                                
 Overview                                                                       
                                                                                
 This is a Job Application Tracker — a personal web app for managing job        
 applications from the Montreal/Business Quad/South Shore area. The stack       
 includes:                                                                      
                                                                                
 ┌─────────────┬───────────────────────────────────────────┐                    
 │ Component   │ Technology                                │                    
 ├─────────────┼───────────────────────────────────────────┤                    
 │ Frontend    │ Single HTML file (CSS + JS embedded)      │                    
 ├─────────────┼───────────────────────────────────────────┤                    
 │ Backend     │ Python http.server built-in HTTP server   │                    
 ├─────────────┼───────────────────────────────────────────┤                    
 │ Storage     │ Markdown files (.md) as database          │                    
 ├─────────────┼───────────────────────────────────────────┤                    
 │ Parsing     │ LinkedIn job report parser (parser.py)    │                    
 ├─────────────┼───────────────────────────────────────────┤                    
 │ Run command │ python job_tracker_server.py on port 8766 │                    
 └─────────────┴───────────────────────────────────────────┘                    
                                                                                
 ────────────────────────────────────────────────────────────────────────────── 
                                                                                
 Critical Issues (Must Fix)                                                     
                                                                                
 ### 1. No CORS / No CSRF Protection                                            
                                                                                
 The server has no security headers at all. Any web page could make API calls   
 against your running server. Add basic middleware:                             
                                                                                
 ```python                                                                      
   def add_security_headers(self):                                              
       self.send_header('Content-Security-Policy', 'frame-ancestors none')      
       self.send_header('X-Content-Type-Options', 'nosniff')                    
                                                                                
   def do_GET(self):                                                            
       self.add_security_headers()                                              
       # ... rest of handler                                                    
 ```                                                                            
                                                                                
 ### 2. No Validation on POST/DELETE                                            
                                                                                
 update_tracker() and delete_application() accept arbitrary JSON with no        
 validation. An attacker (or accidentally malformed client) could:              
 - Create applications with injection-prone content                             
 - Delete indices out of range or negative numbers that crash the app           
                                                                                
 ### 3. Single-File, No Separation of Concerns                                  
                                                                                
 All JS is inline in a single HTML file. This makes testing, debugging, and     
 maintaining impossible as the project grows. Split into separate .js and .css  
 files.                                                                         
                                                                                
 ────────────────────────────────────────────────────────────────────────────── 
                                                                                
 Major Improvements Needed                                                      
                                                                                
 ### 4. Replace Markdown Storage with Proper Database                           
                                                                                
 The current approach (job_applications.md) is fragile:                         
 - ❌ No data integrity constraints                                             
 - ❌ Parsing is regex-based on markdown table format — it breaks if a note     
   contains | or newlines                                                       
 - ❌ No indexing for searches                                                  
 - ❌ No transaction support (partial writes = corruption)                      
                                                                                
 Recommendation: Start with SQLite. It requires zero setup, works in Python     
 natively:                                                                      
                                                                                
 ```python                                                                      
   import sqlite3                                                               
                                                                                
   def init_db():                                                               
       conn = sqlite3.connect('tracker.db')                                     
       c = conn.cursor()                                                        
       c.execute('''CREATE TABLE IF NOT EXISTS applications (                   
           id INTEGER PRIMARY KEY AUTOINCREMENT,                                
           date TEXT NOT NULL,                                                  
           company TEXT NOT NULL,                                               
           title TEXT NOT NULL,                                                 
           source TEXT DEFAULT '',                                              
           status TEXT DEFAULT 'Applied',                                       
           contact TEXT DEFAULT '',                                             
           followUp TEXT DEFAULT '',                                            
           notes TEXT DEFAULT '',                                               
           created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP                       
       )''')                                                                    
 ```                                                                            
                                                                                
 ### 5. Fix Date Logic Race Condition                                           
                                                                                
 In parser.py, job posting dates are computed relatively against                
 datetime.now(). But the markdown file has hardcoded dates from generation time 
 (2026-07-19). If you run the app on a different day, jobs could be             
 reclassified into wrong buckets.                                               
                                                                                
 ### 6. Missing "Edit" Button in UI                                             
                                                                                
 The JavaScript defines editApplication(idx) but it's never called. The table   
 has only:                                                                      
                                                                                
 ```javascript                                                                  
   <button class="btn-icon" onclick="updateStatus(${originalIdx})">🔄</button>  
   <button class="btn-icon"                                                     
 onclick="deleteApplication(${originalIdx})">🗑️</button>                        
   <!-- Missing: edit button -->                                                
 ```                                                                            
                                                                                
 ### 7. No Error Handling / UX Feedback                                         
                                                                                
 - alert() for all errors (annoying, not accessible)                            
 - No loading spinners                                                          
 - No success notification                                                     