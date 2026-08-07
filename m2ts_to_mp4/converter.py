#!/usr/bin/env python3
import sys
import os
import subprocess
import argparse
import tempfile
import shutil
from pathlib import Path

def get_ffmpeg():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    local_ffmpeg = os.path.join(script_dir, "ffmpeg.exe")
    if os.path.exists(local_ffmpeg):
        return local_ffmpeg
    
    local_ffmpeg_nix = os.path.join(script_dir, "ffmpeg")
    if os.path.exists(local_ffmpeg_nix):
        return local_ffmpeg_nix
    
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg:
        return ffmpeg
    
    ffmpeg = shutil.which("ffmpeg.exe")
    if ffmpeg:
        return ffmpeg
    
    common_paths = [
        os.path.expanduser("~\\ffmpeg\\bin\\ffmpeg.exe"),
        os.path.expanduser("~\\Downloads\\ffmpeg\\bin\\ffmpeg.exe"),
        "C:\\ffmpeg\\bin\\ffmpeg.exe",
        "D:\\ffmpeg\\bin\\ffmpeg.exe",
        "E:\\ffmpeg\\bin\\ffmpeg.exe",
    ]
    
    for path in common_paths:
        if os.path.exists(path):
            return path
    
    path_env = os.environ.get("PATH", "")
    for folder in path_env.split(os.pathsep):
        ffmpeg_path = os.path.join(folder, "ffmpeg.exe")
        if os.path.exists(ffmpeg_path):
            return ffmpeg_path
    
    raise FileNotFoundError("ffmpeg not found")

def convert_m2ts_to_mp4(input_path, output_path, video_codec=None, audio_codec=None, 
                        crf=None, preset=None, hw_accel=None, faststart=True,
                        progress_callback=None):
    if not os.path.exists(input_path):
        raise FileNotFoundError(f"Input file not found: {input_path}")
    
    output_dir = os.path.dirname(output_path) or "."
    if output_dir and not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    cmd = [get_ffmpeg(), "-f", "mpegts", "-i", input_path, "-y"]
    
    if video_codec:
        cmd.extend(["-c:v", video_codec])
    else:
        cmd.extend(["-c:v", "copy"])
    
    if audio_codec:
        cmd.extend(["-c:a", audio_codec])
    else:
        cmd.extend(["-c:a", "copy"])
    
    if crf is not None and video_codec and video_codec != "copy":
        cmd.extend(["-crf", str(crf)])
    
    if preset and video_codec and video_codec != "copy":
        cmd.extend(["-preset", preset])
    
    if hw_accel:
        if hw_accel == "cuda":
            cmd.extend(["-hwaccel", "cuda", "-hwaccel_device", "0"])
        elif hw_accel == "videotoolbox":
            cmd.extend(["-hwaccel", "videotoolbox"])
    
    if faststart:
        cmd.extend(["-movflags", "+faststart"])
    
    cmd.extend(["-progress", "pipe:1", output_path])
    
    try:
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
        
        for line in process.stdout:
            if progress_callback and "out_time_ms=" in line:
                try:
                    time_ms = int(line.split("=")[1].strip())
                    progress_callback(time_ms / 1000000)
                except:
                    pass
        
        process.wait()
        
        if process.returncode != 0:
            stderr = process.stderr.read()
            raise RuntimeError(f"ffmpeg failed: {stderr}")
        
        return True
        
    except FileNotFoundError as e:
        raise RuntimeError(f"ffmpeg not found: {e}\n\nInstall ffmpeg:\n- Windows: https://www.gyan.dev/ffmpeg/builds/ (download ffmpeg-release-essentials.zip)\n- macOS: brew install ffmpeg\n- Linux: sudo apt install ffmpeg\n\nAfter install, add ffmpeg to PATH or put ffmpeg.exe in C:\\ffmpeg\\bin\\")

def generate_subtitles(video_path, output_srt=None, model_size="medium", progress_callback=None):
    if not os.path.exists(video_path):
        raise FileNotFoundError(f"Video file not found: {video_path}")
    
    try:
        from faster_whisper import WhisperModel
        from deep_translator import GoogleTranslator
    except ImportError:
        print("Error: faster-whisper and deep-translator required for subtitles")
        print("Install: pip install faster-whisper deep-translator")
        return None
    
    if output_srt is None:
        output_srt = str(Path(video_path).with_suffix(".srt"))
    
    audio_path = tempfile.mktemp(suffix=".wav")
    
    try:
        if progress_callback:
            progress_callback(5)
        
        cmd = ["ffmpeg", "-y", "-i", video_path, "-vn", "-acodec", "pcm_s16le", "-ar", "16000", "-ac", "1", audio_path]
        subprocess.run(cmd, check=True, capture_output=True)
        
        if progress_callback:
            progress_callback(15)
        
        print(f"Transcribing with Whisper {model_size}...")
        model = WhisperModel(model_size, device="cpu", compute_type="float32")
        
        segments, info = model.transcribe(
            audio_path,
            language="ja",
            beam_size=5,
            vad_filter=True
        )
        
        results = []
        for segment in segments:
            results.append({
                "start": segment.start,
                "end": segment.end,
                "text": segment.text.strip()
            })
        
        if progress_callback:
            progress_callback(40)
        
        print(f"Translating {len(results)} segments to English...")
        translator = GoogleTranslator(source="ja", target="en")
        
        translated = []
        for i, seg in enumerate(results):
            english_text = translator.translate(seg["text"])
            translated.append({
                "start": seg["start"],
                "end": seg["end"],
                "text": english_text
            })
            if progress_callback:
                progress_callback(40 + (i / len(results)) * 55)
        
        with open(output_srt, "w", encoding="utf-8") as f:
            for i, seg in enumerate(translated, 1):
                start_time = format_time(seg["start"])
                end_time = format_time(seg["end"])
                f.write(f"{i}\n")
                f.write(f"{start_time} --> {end_time}\n")
                f.write(f"{seg['text']}\n\n")
        
        if progress_callback:
            progress_callback(100)
        
        return output_srt
        
    finally:
        if os.path.exists(audio_path):
            os.remove(audio_path)

def format_time(seconds):
    hours = int(seconds // 3600)
    minutes = int((seconds % 3600) // 60)
    secs = int(seconds % 60)
    millis = int((seconds % 1) * 1000)
    return f"{hours:02d}:{minutes:02d}:{secs:02d},{millis:03d}"

def main():
    parser = argparse.ArgumentParser(description="Convert m2ts to mp4 with optional Japanese->English subtitles")
    parser.add_argument("input", help="Input m2ts file")
    parser.add_argument("-o", "--output", help="Output mp4 file")
    parser.add_argument("-c", "--crf", type=int, help="CRF value for re-encoding")
    parser.add_argument("-p", "--preset", default="medium", 
                       choices=["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"],
                       help="Encoding preset")
    parser.add_argument("--hw", "--hw-accel", dest="hw_accel", 
                       choices=["cuda", "videotoolbox", "none"],
                       help="Hardware acceleration")
    parser.add_argument("-v", "--video-codec", dest="video_codec", 
                       choices=["copy", "libx264", "libx265", "hevc_videotoolbox"],
                       help="Video codec (default: copy)")
    parser.add_argument("-a", "--audio-codec", dest="audio_codec",
                       choices=["copy", "aac", "libmp3lame"],
                       help="Audio codec (default: copy)")
    parser.add_argument("--no-faststart", dest="no_faststart", action="store_true",
                       help="Disable faststart")
    parser.add_argument("--srt", dest="generate_srt", action="store_true",
                       help="Generate Japanese->English SRT subtitle")
    parser.add_argument("--srt-model", dest="srt_model", default="medium",
                       choices=["tiny", "base", "small", "medium", "large"],
                       help="Whisper model size")
    
    args = parser.parse_args()
    
    input_path = os.path.abspath(args.input)
    
    if args.output:
        output_path = os.path.abspath(args.output)
    else:
        base = os.path.splitext(input_path)[0]
        output_path = base + ".mp4"
    
    video_codec = args.video_codec or "copy"
    audio_codec = args.audio_codec or "copy"
    
    print(f"Input: {input_path}")
    print(f"Output: {output_path}")
    print(f"Video: {video_codec}, Audio: {audio_codec}")
    
    def show_progress(sec):
        print(f"\rProgress: {sec:.0f}s", end="", flush=True)
    
    try:
        # If only generating SRT (no m2ts input), skip conversion
        if args.generate_srt and os.path.splitext(input_path)[1].lower() == ".mp4":
            srt_path = str(Path(input_path).with_suffix(".srt"))
            print(f"\nGenerating subtitles: {srt_path}")
            generate_subtitles(
                input_path,
                output_srt=srt_path,
                model_size=args.srt_model,
                progress_callback=lambda p: print(f"\rSubtitle: {p:.1f}%", end="", flush=True)
            )
            print(f"\nSubtitle saved: {srt_path}")
            return
            
        convert_m2ts_to_mp4(
            input_path,
            output_path,
            video_codec=video_codec,
            audio_codec=audio_codec,
            crf=args.crf,
            preset=args.preset,
            hw_accel=args.hw_accel if args.hw_accel != "none" else None,
            faststart=not args.no_faststart,
            progress_callback=show_progress
        )
        print(f"\nDone: {output_path}")
        
        if args.generate_srt:
            srt_path = str(Path(output_path).with_suffix(".srt"))
            print(f"\nGenerating subtitles: {srt_path}")
            generate_subtitles(
                output_path,
                output_srt=srt_path,
                model_size=args.srt_model,
                progress_callback=lambda p: print(f"\rSubtitle: {p:.1f}%", end="", flush=True)
            )
            print(f"\nSubtitle saved: {srt_path}")
            
    except Exception as e:
        print(f"\nError: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()