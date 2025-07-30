import re
import csv
import sys

def parse_to_csv(input_file, output_file):
    with open(input_file, 'r') as f:
        content = f.read()
    
    # Parse comparison data
    comparison_pattern = r'COMPARISON: SIZE:(\d+) PATTERN:(\d+) LUSTRE_ALLOC_NS:(\d+) REGULAR_ALLOC_NS:(\d+) RATIO:([\d.]+)'
    matches = re.findall(comparison_pattern, content)
    
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(['Size', 'Pattern', 'Lustre_Alloc_NS', 'Regular_Alloc_NS', 'Ratio'])
        
        for match in matches:
            writer.writerow([
                int(match[0]),  # size
                int(match[1]),  # pattern
                int(match[2]),  # lustre_alloc_ns
                int(match[3]),  # regular_alloc_ns
                float(match[4])  # ratio
            ])

if __name__ == "__main__":
    parse_to_csv(sys.argv[1], sys.argv[2])
