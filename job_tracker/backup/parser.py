import os
import re
from datetime import datetime

# South Shore cities (highest priority)
SOUTH_SHORE = ['brossard', 'longueuil', 'saint-lambert', 'boucherville', 'saint-hubert', 
               'saint-bruno', 'la prairie', 'saint-constant', 'chambly', 'candiac', 
               'delson', 'sainte-catherine', 'greenfield park']

def parse_linkedin_report(report_file_path):
    """Parse the LinkedIn report and return pending jobs with date info for grouping"""
    if not os.path.exists(report_file_path):
        return [], ''
    
    with open(report_file_path, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Extract the report generation date from the file header
    report_date = ''
    generated_match = re.search(r'\*\*Generated:\*\* (\d{4}-\d{2}-\d{2})', content)
    if generated_match:
        report_date = generated_match.group(1)
    else:
        report_date = datetime.now().strftime('%Y-%m-%d')
    
    # Map month names to numbers
    month_map = {
        'jan': 1, 'feb': 2, 'mar': 3, 'apr': 4, 'may': 5, 'jun': 6,
        'jul': 7, 'aug': 8, 'sep': 9, 'oct': 10, 'nov': 11, 'dec': 12
    }
    
    def parse_posted_date(posted_str):
        """Parse posted date like 'Jul 15' relative to TODAY"""
        if not posted_str:
            return 3, posted_str
        posted_str = posted_str.strip().lower()
        today = datetime.now()
        try:
            parts = posted_str.split()
            if len(parts) >= 2:
                month_str = parts[0][:3]
                day = int(parts[1])
                month = month_map.get(month_str, 7)
                year = today.year
                posted_date = datetime(year, month, day)
                if posted_date.date() > today.date():
                    posted_date = datetime(year - 1, month, day)
                days_ago = (today.date() - posted_date.date()).days

                if days_ago == 0:
                    return 0, posted_str
                elif days_ago == 1:
                    return 1, posted_str
                elif days_ago <= 7:
                    return 2, posted_str
                else:
                    return 3, posted_str
        except:
            pass
        return 3, posted_str

    def normalize_job_link(link_str):
        """Normalize markdown links and detect easy apply labels."""
        if not link_str:
            return '', False
        link_str = link_str.strip()
        match = re.match(r'\[([^\]]+)\]\((https?://[^\)]+)\)', link_str)
        if match:
            label = match.group(1).strip().lower()
            url = match.group(2).strip()
            return url, 'easy apply' in label
        return link_str.replace(')', '').strip(), 'easy apply' in link_str.lower()

    jobs = []
    lines = content.split('\n')
    in_senior = False
    in_fullstack = False
    
    for line in lines:
        if line.startswith('## Senior Developer'):
            in_senior = True
            in_fullstack = False
            continue
        if line.startswith('## Full Stack Developer'):
            in_fullstack = True
            in_senior = False
            continue
        if line.startswith('##'):
            in_senior = False
            in_fullstack = False
            
        if line.startswith('|') and not line.startswith('| #') and not line.startswith('|---'):
            parts = [p.strip() for p in line.split('|')]
            if len(parts) >= 7 and parts[1] and parts[1].isdigit():
                try:
                    job_num = int(parts[1])
                    if parts[2] == '---' or parts[2].startswith('---'):
                        continue
                    fit = parts[2].strip()
                    fit_score = 0
                    if fit.startswith('⭐'):
                        fit_score = int(fit.replace('⭐', '').replace('**', '').strip())
                    elif fit.startswith(' '):
                        fit_score = int(fit.strip()) if fit.strip().lstrip('-').isdigit() else 0
                    
                    title = parts[3].strip()
                    company = parts[4].strip()
                    location = parts[5].strip()
                    area = parts[6].strip()
                    posted = parts[7].strip()
                    raw_link = parts[8].strip() if len(parts) > 8 else ''
                    link, easy_apply = normalize_job_link(raw_link)
                    
                    # Determine date bucket and display date
                    date_bucket, display_date = parse_posted_date(posted)
                    
                    # Determine area priority
                    area_lower = area.lower()
                    if 'south shore' in area_lower or 'brossard' in location.lower():
                        priority = 1
                    elif 'montreal' in area_lower or 'montreal' in location.lower():
                        priority = 2
                    elif 'laval' in area_lower:
                        priority = 4
                    else:
                        priority = 3
                    
                    jobs.append({
                        'num': job_num,
                        'fit': fit_score,
                        'title': title,
                        'company': company,
                        'location': location,
                        'area': area,
                        'posted': posted,
                        'display_date': display_date,
                        'link': link,
                        'easy_apply': easy_apply,
                        'priority': priority,
                        'date_bucket': date_bucket,
                        'section': 'Senior' if in_senior else 'Full Stack'
                    })
                except:
                    pass
    
    # Remove duplicates (keep newest/best)
    unique_jobs = {}
    for job in jobs:
        key = (
            re.sub(r'\s+', ' ', job['company']).strip().casefold(),
            re.sub(r'\s+', ' ', job['title']).strip().casefold(),
        )
        existing = unique_jobs.get(key)
        if existing is None:
            unique_jobs[key] = job
            continue

        should_replace = False
        if existing['link'] and not job['link']:
            should_replace = False
        elif not existing['link'] and job['link']:
            should_replace = True
        elif job['date_bucket'] < existing['date_bucket']:
            should_replace = True
        elif job['date_bucket'] == existing['date_bucket'] and job['fit'] > existing['fit']:
            should_replace = True
        elif job['date_bucket'] == existing['date_bucket'] and job['fit'] == existing['fit'] and job['priority'] < existing['priority']:
            should_replace = True

        if should_replace:
            unique_jobs[key] = job

    # Sort
    jobs = list(unique_jobs.values())
    jobs.sort(key=lambda x: (x['priority'], -x['fit']))

    return jobs, report_date