import time

import paho.mqtt.client as mqtt
import cv2
from pathlib import Path
from imutils import paths
import face_recognition

def load_known_faces(known_face_dir):
    known_faces = []
    known_names = []

    for image_path in paths.list_images(known_face_dir):
        name = Path(image_path).stem
        image = face_recognition.load_image_file(image_path)
        encodings = face_recognition.face_encodings(image)

        if encodings:
            known_faces.append(encodings[0])
            known_names.append(name)

    return known_faces, known_names

def compare_faces_with_database(input_image_path, known_faces):
    unknown_image = face_recognition.load_image_file(input_image_path)
    unknown_encodings = face_recognition.face_encodings(unknown_image)

    if not unknown_encodings:
        print("No face detected in the input image.")
        return True

    unknown_encoding = unknown_encodings[0]
    results = face_recognition.compare_faces(known_faces, unknown_encoding, tolerance=0.6)
    return not any(results)

def is_unkonwn_person():
    known_face_database_dir = 'pictures'
    test_image_path = './input.jpg'
    known_faces, known_names = load_known_faces(known_face_database_dir)
    return compare_faces_with_database(test_image_path, known_faces)

def write_text_on_image(image_path, text, color):
    image = cv2.imread(image_path)
    position = (50, 50)
    font = cv2.FONT_HERSHEY_SIMPLEX
    font_scale = 2
    thickness = 2
    cv2.putText(image, text, position, font, font_scale, color, thickness)
    cv2.imwrite(image_path, image)

def on_message(client, userdata, message):
    if message.payload.decode() == "get_pic":
        print("Received 'get_pic' command. Capturing image...")
        cap = cv2.VideoCapture(0)
        if not cap.isOpened():
            print("Error: Could not open webcam.")
            return

        time.sleep(1)

        ret, frame = cap.read()

        if ret:
            filename = "./input.jpg"
            cv2.imwrite(filename, frame)
            print(f"Image saved as {filename}")
        else:
            print("Error: Could not capture image.")
            cap.release()
            return

        unknown = is_unkonwn_person()
        if unknown:
            client.publish("auth/status", "Unknown person")
            write_text_on_image(filename, "THE PERSON IS UNKNOWN", (0, 0, 255))
        else:
            client.publish("auth/status", "Known person")
            write_text_on_image(filename, "THE PERSON IS KNOWN", (0, 255, 0))

        cap.release()
        cv2.destroyAllWindows()

        client.publish("image", "input.jpg")

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code", rc)
    client.subscribe("auth/status")

broker = "192.168.175.187"
client = mqtt.Client()
client.username_pw_set("uname", "upass")
client.on_connect = on_connect
client.on_message = on_message

client.connect(broker, 1883, 60)

print("Listening for 'get_pic' messages on 'camera/control'...")
client.loop_forever()
