from flask import Flask, request, jsonify
from flask_socketio import SocketIO
import threading
import time
import cv2
import os
from flask import send_from_directory

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

# In-memory storage for user data
user_data_store = {}

camera_urls = []  # URL kamera dinamis, diatur melalui /camera endpoint

# Sublevel configuration: levels and sublevels
sublevels_per_level = [7, 8, 8, 9, 9, 10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16]
sublevel_times = [9.00, 8.00, 7.58, 7.20, 6.86, 6.55, 6.26, 6.00, 5.76, 5.54,
                  5.33, 5.14, 4.97, 4.80, 4.65, 4.50, 4.36, 4.24, 4.11, 4.00, 3.89]

# Global variables to track the current level and sublevel
current_level = 0
current_sublevel = 0
process_running = False
camera_toggle = 1  # Toggle antara kamera 1 dan kamera 2
last_sensor = None

# Directory for temporary image storage

IMAGE_DIR = "temp_images"
if not os.path.exists(IMAGE_DIR):
    os.makedirs(IMAGE_DIR)

def get_default_user_data():
    return {
        "infrared": [[None] * sublevels_per_level[level] for level in range(len(sublevels_per_level))],
        "current_sensor": None,  # Track last triggered sensor (A/B)
        "level": 0,
        "shuttle": 0,
        "monitor_data": []  # Store monitor data (heart rate and pulse oximeter)
    }

@app.route('/infrared', methods=['POST'])
def receive_infrared_data():
    """
    Endpoint to receive infrared data from sensors.
    """
    data = request.json
    sensor_id = data.get("sensorID")  # e.g., "cone0-A" or "cone0-B"

    if not sensor_id or "-" not in sensor_id:
        return jsonify({"message": "Invalid sensorID format", "status": "error"}), 400

    # Normalize sensorID to group A/B sensors into the same user
    cone_id = sensor_id.split("-")[0]  # Extract "cone0" from "cone0-A"
    user_id = f"user_{cone_id[4:]}"    # Map to "user_0"

    # Ensure user exists in the data store
    if user_id not in user_data_store:
        user_data_store[user_id] = get_default_user_data()

    # Track which sensor sent the data (only "A" or "B")
    user_data_store[user_id]['current_sensor'] = sensor_id[-1]  # "A" or "B"

    # Update infrared array
    current_level = user_data_store[user_id]['level'] - 1
    current_shuttle = user_data_store[user_id]['shuttle'] - 1
    user_data_store[user_id]["infrared"][current_level][current_shuttle] = data["infrared"]

    print(f"[INFO] Updated {user_id} with data from {sensor_id}")

    return jsonify({"message": "Infrared data received", "status": "success"}), 200

@app.route('/monitor', methods=['POST'])
def receive_monitor_data():
    """
    Endpoint to receive heart rate and pulse oximeter data from monitor devices.
    """
    data = request.json
    monitor_id = data.get("monitorID")  # Use monitorID from the payload
    heart_rate = data.get("heartRate")
    pulse_oximeter = data.get("pulseOximeter")

    # Validate incoming data
    if not monitor_id or heart_rate is None or pulse_oximeter is None:
        return jsonify({"message": "Invalid monitor data", "status": "error"}), 400

    # Normalize monitorID to a user key
    monitor_number = int(monitor_id[7:])  # Extract number from "monitor0"
    user_id = f"user_{monitor_number}"    # Example: "monitor0" -> "user_0"

    # Ensure the user exists in the data store
    if user_id not in user_data_store:
        user_data_store[user_id] = get_default_user_data()

    # Append monitor data
    user_data_store[user_id]["monitor_data"].append({
        "time": time.strftime("%Y-%m-%d %H:%M:%S"),
        "heart_rate": heart_rate,
        "pulse_oximeter": pulse_oximeter
    })
    print(f"[MONITOR] Data received for {user_id} from {monitor_id}: "
          f"Heart Rate={heart_rate}, Pulse Oximeter={pulse_oximeter}")

    return jsonify({"message": f"Monitor data received from {monitor_id}", "status": "success"}), 200

def capture_image(camera_url, level, sublevel):
    """
    Capture an image from the given camera URL, draw a red vertical line at the center,
    and save it temporarily.
    """
    try:
        cap = cv2.VideoCapture(camera_url)
        print(f"[DEBUG] Is camera opened: {cap.isOpened()}")
        ret, frame = cap.read()
        if ret:
            frame = cv2.resize(frame, (640, 480))  # Resize for uniformity
            center_x = frame.shape[1] // 2
            
            # Draw red vertical line
            cv2.line(frame, (center_x, 0), (center_x, frame.shape[0]), (0, 0, 255), 2)
            
            # Save the image
            image_name = f"level_{level}_sublevel_{sublevel}.jpg"
            image_path = os.path.join(IMAGE_DIR, image_name)
            success = cv2.imwrite(image_path, frame)

            if success:
                print(f"[CAPTURED] Image with line saved: {image_path}")
            else:
                print(f"[ERROR] Failed to save the image.")
            
            cap.release()
            return image_path
        else:
            cap.release()
            print(f"[ERROR] Failed to capture image from {camera_url}")
            return None
    except Exception as e:
        print(f"[ERROR] Failed to capture image: {e}")
        return None

def process_levels_and_sublevels():
    global current_level, current_sublevel, process_running, camera_toggle

    print("[INFO] Waiting for 13 seconds to synchronize with the application...")
    time.sleep(13)

    sublevel_index = 0  # Initialize the sublevel index

    # Track consecutive False counters
    consecutive_false_count = {user: 0 for user in user_data_store}

    while process_running:
        if not camera_urls or len(camera_urls) < 2:
            print("[ERROR] Camera URLs not set. Please POST to /camera first.")
            process_running = False
            return

        expected_sensor = "B" if sublevel_index % 2 == 0 else "A"
        sublevel_time = sublevel_times[current_level]

        print(f"[INFO] Level {current_level + 1}, Sublevel {current_sublevel + 1} "
              f"(Expected: {expected_sensor}, Time: {sublevel_time} sec)")

        sensor_triggered = False

        for _ in range(int(sublevel_time * 10)):  # Check every 100ms within sublevel_time
            time.sleep(0.1)

            if not process_running:
                print("[INFO] Process stopped mid-execution.")
                return

            for user in user_data_store:
                last_sensor = user_data_store[user].get("current_sensor")
                if last_sensor == expected_sensor:
                    sensor_triggered = True
                    user_data_store[user]["current_sensor"] = None  # Reset the sensor
                    consecutive_false_count[user] = 0  # Reset False counter
                    print(f"[INFO] {user} triggered Sensor {expected_sensor}")

        # Capture image
        camera_url = camera_urls[camera_toggle % 2]
        image_path = capture_image(camera_url, current_level + 1, current_sublevel + 1)
        camera_toggle += 1

        # Update the infrared array and check conditions
        if not process_running:
            return

        all_true = True  # Check if all users have True infrared data

        for user in user_data_store:
            user_data_store[user]["level"] = current_level + 1
            user_data_store[user]["shuttle"] = current_sublevel + 1

            if sensor_triggered:
                user_data_store[user]["infrared"][current_level][current_sublevel] = True
            else:
                user_data_store[user]["infrared"][current_level][current_sublevel] = False
                consecutive_false_count[user] += 1
                all_true = False  # At least one user has False value
                print(f"[MISS] {user} consecutive False count: {consecutive_false_count[user]}")

        # Remove image if all infrared values are True
        if all_true and image_path:
            try:
                os.remove(image_path)
                print(f"[DELETED] Image removed as all infrared values are True: {image_path}")
            except FileNotFoundError:
                print(f"[ERROR] File not found for deletion: {image_path}")

        # Check stop condition: if any user has 2 consecutive False and no one triggered
        if any(count >= 2 for count in consecutive_false_count.values()) and not sensor_triggered:
            print("[INFO] A user has 2 consecutive False values. Stopping the test.")
            process_running = False
            return

        # Check if all levels are completed
        current_sublevel += 1
        sublevel_index += 1
        if current_sublevel >= sublevels_per_level[current_level]:
            current_sublevel = 0
            current_level += 1
            if current_level >= len(sublevels_per_level):
                process_running = False
                print("[INFO] All levels completed.")
                return

@app.route('/start_process', methods=['POST'])
def start_process():
    """
    Start the test process and clear monitor data arrays.
    """
    global process_running
    if not process_running:
        process_running = True

        # Clear monitor data for all users
        for user in user_data_store:
            user_data_store[user]["monitor_data"] = []

        threading.Thread(target=process_levels_and_sublevels, daemon=True).start()
        print("[INFO] Test started, monitor data cleared for all users.")
        return jsonify({"message": "Process started", "status": "success"}), 200
    else:
        return jsonify({"message": "Process already running", "status": "error"}), 400

@app.route('/stop_process', methods=['POST'])
def stop_process():
    """
    Endpoint to stop the running test process, print user infrared arrays, and clear the arrays.
    """
    global process_running, current_level, current_sublevel
    process_running = False

    print("\n[INFO] Process stopped. Printing and clearing user infrared arrays:")
    for user, data in user_data_store.items():
        # Print the current infrared array for the user
        print(f"\nUser: {user}")
        print("Infrared Array (Levels x Sublevels):")
        for level_idx, sublevels in enumerate(data["infrared"]):
            print(f"  Level {level_idx + 1}: {sublevels}")

    print("\n[INFO] Process stopped. Printing and clearing user monitor data:")
    for user, data in user_data_store.items():
        print(f"\nUser: {user}")
        print("Monitor Data (Time, Heart Rate, Pulse Oximeter):")
        for entry in data["monitor_data"]:
            print(f"  Time: {entry['time']}, Heart Rate: {entry['heart_rate']}, Pulse Oximeter: {entry['pulse_oximeter']}")

    # Reset current level and sublevel
    current_level = 0
    current_sublevel = 0
    print(f"[INFO] Reset current_level and current_sublevel to 0.")

    # Add Debug Statement Here
    print("[DEBUG] Final user data:", user_data_store)

    return jsonify({"message": "Process stopped and arrays cleared", "status": "success"}), 200

@app.route('/clear_data', methods=['POST'])
def clear_data():
    """
    Endpoint to clear user data without stopping the process.
    """
    global user_data_store
    # user_data_store = {}

    # Clear only specific parts of the data (monitor_data and infrared)
    for user in user_data_store:
        user_data_store[user]["infrared"] = [
            [None] * sublevels_per_level[level] for level in range(len(sublevels_per_level))
        ]
        user_data_store[user]["monitor_data"] = []
        print(f"[INFO] Cleared data for {user}")

    return jsonify({"message": "User data cleared successfully", "status": "success"}), 200


@app.route('/data/<user>', methods=['GET'])
def get_user_data(user):
    """
    Endpoint to fetch the most recent combined data for a user.
    """
    data = user_data_store.get(user)
    if not data:
        return jsonify({"message": "User not found", "status": "error"}), 404
    
    response_data = {
        "infrared": data.get("infrared", False),
        "heartRate": data.get("heartRate"),
        "pulseOximeter": data.get("pulseOximeter"),
        "level": data.get("level"),
        "shuttle": data.get("shuttle")
    }
    return jsonify(response_data), 200

@app.route('/data', methods=['GET'])
def get_all_users_data():
    """
    Endpoint to fetch data for all users with average heart rate and pulse oximeter.
    """
    response = {}
    for user, data in user_data_store.items():
        # Extract monitor_data list
        monitor_data = data.get("monitor_data", [])
        
        # Initialize variables for summing and counting values
        total_heart_rate = 0
        total_pulse_oximeter = 0
        count = 0
        
        # Calculate sums and count valid entries
        for entry in monitor_data:
            heart_rate = entry.get("heart_rate")
            pulse_oximeter = entry.get("pulse_oximeter")
            
            if heart_rate is not None and isinstance(heart_rate, (int, float)):
                total_heart_rate += heart_rate
            if pulse_oximeter is not None and isinstance(pulse_oximeter, (int, float)):
                total_pulse_oximeter += pulse_oximeter
            count += 1
        
        # Calculate averages (avoid division by zero)
        avg_heart_rate = round(total_heart_rate / count, 2) if count > 0 else None
        avg_pulse_oximeter = round(total_pulse_oximeter / count, 2) if count > 0 else None
        
        # Add extracted data into the response
        response[user] = {
            "infrared": data.get("infrared", False),
            "average_heartRate": avg_heart_rate,  # Average heart rate
            "average_pulseOximeter": avg_pulse_oximeter,  # Average pulse oximeter
            "monitor_data": monitor_data,
            "level": data.get("level"),
            "shuttle": data.get("shuttle")
        }

    # Print the response before returning
    print("Response to /data endpoint with averages:", response)
    return jsonify(response), 200

@app.route('/camera', methods=['POST'])
def receive_camera_data():
    """
    Endpoint to receive camera IP URLs.
    """
    global camera_urls
    data = request.json
    url1 = data.get("url1")
    url2 = data.get("url2")

    if url1 and url2:
        camera_urls = [url1, url2]
        print(f"[CAMERA] Camera URLs updated: {camera_urls}")
        return jsonify({"message": "Camera URLs updated", "status": "success"}), 200
    else:
        return jsonify({"message": "Invalid camera URLs", "status": "error"}), 400

# Route to serve images from the temp_images directory
@app.route('/images/<filename>', methods=['GET'])
def get_image(filename):
    """
    Serve the image file from the IMAGE_DIR.
    """
    try:
        return send_from_directory(IMAGE_DIR, filename)
    except FileNotFoundError:
        return jsonify({"message": "Image not found", "status": "error"}), 404

if __name__ == '__main__':
    # # Add Debug Statement Here
    # print("[DEBUG] Final user data:", user_data_store)
    socketio.run(app, debug=True, host='0.0.0.0', port=5000)
