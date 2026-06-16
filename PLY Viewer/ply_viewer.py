#!/usr/bin/env python3
"""
PLY Point Cloud Viewer
A simple application to preview PLY (point cloud) 3D files.
"""

import sys
import os
import tkinter as tk
from tkinter import filedialog, messagebox
import open3d as o3d
import numpy as np


class PLYViewer:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("PLY Point Cloud Viewer")
        self.root.geometry("400x200")
        self.root.resizable(False, False)
        
        # Center the window
        self.root.eval('tk::PlaceWindow . center')
        
        self.setup_ui()
        
    def setup_ui(self):
        # Main frame
        main_frame = tk.Frame(self.root, padx=20, pady=20)
        main_frame.pack(expand=True, fill='both')
        
        # Title label
        title_label = tk.Label(
            main_frame, 
            text="PLY Point Cloud Viewer",
            font=('Arial', 16, 'bold')
        )
        title_label.pack(pady=(0, 20))
        
        # Description
        desc_label = tk.Label(
            main_frame,
            text="Open and visualize PLY point cloud files in 3D",
            font=('Arial', 10)
        )
        desc_label.pack(pady=(0, 20))
        
        # Buttons frame
        btn_frame = tk.Frame(main_frame)
        btn_frame.pack()
        
        # Open file button
        open_btn = tk.Button(
            btn_frame,
            text="Open PLY File",
            command=self.open_file,
            width=15,
            height=2,
            bg='#4CAF50',
            fg='white',
            font=('Arial', 10, 'bold')
        )
        open_btn.pack(side='left', padx=5)
        
        # Exit button
        exit_btn = tk.Button(
            btn_frame,
            text="Exit",
            command=self.root.quit,
            width=10,
            height=2,
            font=('Arial', 10)
        )
        exit_btn.pack(side='left', padx=5)
        
        # Instructions
        instructions = tk.Label(
            main_frame,
            text="Controls: Left-click drag to rotate, Right-click drag to pan, Scroll to zoom",
            font=('Arial', 8),
            fg='gray'
        )
        instructions.pack(pady=(20, 0))
        
    def open_file(self):
        file_path = filedialog.askopenfilename(
            title="Select PLY File",
            filetypes=[
                ("PLY files", "*.ply"),
                ("All files", "*.*")
            ]
        )
        
        if file_path:
            self.view_ply(file_path)
    
    def view_ply(self, file_path):
        try:
            # Read the point cloud
            pcd = o3d.io.read_point_cloud(file_path)
            
            if pcd.is_empty():
                messagebox.showerror("Error", "The PLY file is empty or could not be read.")
                return
            
            # Get point cloud info
            num_points = len(pcd.points)
            has_colors = pcd.has_colors()
            has_normals = pcd.has_normals()
            
            info_msg = f"Points: {num_points:,}"
            if has_colors:
                info_msg += " | Has Colors"
            if has_normals:
                info_msg += " | Has Normals"
            
            print(f"Loaded: {os.path.basename(file_path)}")
            print(info_msg)
            
            # If no colors, add a gradient color based on height (Z)
            if not has_colors:
                points = np.asarray(pcd.points)
                z_vals = points[:, 2]
                z_min, z_max = z_vals.min(), z_vals.max()
                if z_max - z_min > 0:
                    z_normalized = (z_vals - z_min) / (z_max - z_min)
                else:
                    z_normalized = np.zeros_like(z_vals)
                
                # Create a colormap (blue to red gradient)
                colors = np.zeros((len(points), 3))
                colors[:, 0] = z_normalized  # Red channel
                colors[:, 2] = 1 - z_normalized  # Blue channel
                pcd.colors = o3d.utility.Vector3dVector(colors)
            
            # Estimate normals if not present (for better visualization)
            if not has_normals:
                pcd.estimate_normals(
                    search_param=o3d.geometry.KDTreeSearchParamHybrid(radius=0.1, max_nn=30)
                )
            
            # Create visualizer
            vis = o3d.visualization.Visualizer()
            vis.create_window(
                window_name=f"PLY Viewer - {os.path.basename(file_path)}",
                width=1280,
                height=720
            )
            
            # Add geometry
            vis.add_geometry(pcd)
            
            # Set rendering options
            opt = vis.get_render_option()
            opt.background_color = np.array([0.1, 0.1, 0.1])  # Dark background
            opt.point_size = 1.0  # Reduced point size for smoother appearance
            opt.show_coordinate_frame = True
            opt.light_on = True  # Enable lighting shading for better depth
            
            # Set view point initially to frame the object
            vis.reset_view_point(True)
            
            # Move camera distant normal (zoom out)
            view_control = vis.get_view_control()
            view_control.set_zoom(1.5)
            
            # Run visualizer
            vis.run()
            vis.destroy_window()
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load PLY file:\n{str(e)}")
    
    def run(self):
        self.root.mainloop()


def main():
    # Check for command line argument
    if len(sys.argv) > 1:
        file_path = sys.argv[1]
        if os.path.exists(file_path) and file_path.lower().endswith('.ply'):
            viewer = PLYViewer()
            viewer.view_ply(file_path)
            viewer.run()
        else:
            print(f"Error: Invalid file path or not a PLY file: {file_path}")
            sys.exit(1)
    else:
        # Open GUI
        viewer = PLYViewer()
        viewer.run()


if __name__ == "__main__":
    main()
