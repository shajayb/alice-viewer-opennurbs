import PIL.Image
import os
import numpy as np

def analyze_image(path):
    if not os.path.exists(path):
        return None
    
    img = PIL.Image.open(path).convert('RGB')
    data = np.array(img)
    
    # Background color is (150, 150, 150)
    bg_color = np.array([150, 150, 150])
    black = np.array([0, 0, 0])
    
    # Count pixels that are NOT background and NOT black
    # Using a small tolerance for "almost background"
    tol = 5
    is_bg = np.all(np.abs(data - bg_color) <= tol, axis=-1)
    is_black = np.all(np.abs(data - black) <= tol, axis=-1)
    
    total_pixels = data.shape[0] * data.shape[1]
    non_bg_non_black_pixels = np.sum(~(is_bg | is_black))
    
    coverage = (non_bg_non_black_pixels / total_pixels) * 100
    
    # Check for "complex geometry" by looking at color variance in non-bg areas
    if non_bg_non_black_pixels > 0:
        mask = ~(is_bg | is_black)
        variance = np.var(data[mask])
    else:
        variance = 0
        
    return {
        "path": path,
        "coverage_pct": coverage,
        "variance": variance,
        "size": os.path.getsize(path)
    }

files_to_check = [
    "build/bin/OUTPUT/streamed_gepr_loc0_view0.png",
    "build/bin/OUTPUT/streamed_gepr_loc0_view1.png",
    "build/bin/OUTPUT/streamed_gepr_loc0_view2.png",
    "build/bin/OUTPUT/streamed_gepr_loc0_view3.png",
    "build/bin/OUTPUT/streamed_prod_4k_loc0_view3_beauty.png"
]

results = []
for f in files_to_check:
    res = analyze_image(f)
    if res:
        results.append(res)
    else:
        print(f"File not found: {f}")

for res in results:
    print(f"File: {res['path']}")
    print(f"  Coverage: {res['coverage_pct']:.2f}%")
    print(f"  Variance: {res['variance']:.2f}")
    print(f"  Size: {res['size']} bytes")
    print("-" * 20)
