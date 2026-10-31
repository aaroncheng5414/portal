
import cv2
from ultralytics import YOLO

# Load a pretrained YOLO model
model = YOLO("yolo26n.pt")

# Open default webcam
cap = cv2.VideoCapture(1)

if not cap.isOpened():
    raise RuntimeError("Could not open webcam")

while True:
    # Read frame from webcam
    ret, frame = cap.read()

    if not ret:
        break

    # Run object detection
    results = model(frame, verbose=False)

    # Draw bounding boxes and labels
    annotated_frame = results[0].plot()

    # Display the result
    cv2.imshow("YOLO Object Detection", annotated_frame)

    # Press Q to exit
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
