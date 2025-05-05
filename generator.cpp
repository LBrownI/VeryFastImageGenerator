#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>

/**
 * @brief Generates a random color image of the specified dimensions.
 *
 * Creates an 8-bit, 3-channel (BGR format by default in OpenCV) image
 * and fills each pixel's channels with random values between 0 and 255.
 *
 * @param width The desired width of the image in pixels. Must be positive.
 * @param height The desired height of the image in pixels. Must be positive.
 * @return cv::Mat An OpenCV matrix representing the generated random image.
 * Returns an empty cv::Mat if width or height are not positive.
 */
cv::Mat generateRandomImage(int width, int height) {
  if (width <= 0 || height <= 0) {
    std::cerr << "Error: Image dimensions must be positive." << std::endl;
    return cv::Mat(); // Return an empty Mat to indicate failure
  }

  // Create an 8-bit 3-channel BGR image (Height x Width)
  cv::Mat randomImage(height, width, CV_8UC3);

  // Fill the image with random values (0-255 for each channel)
  cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));

  return randomImage;
}

int main(int argc, char **argv) {
  int imageWidth = 640;
  int imageHeight = 480;

  std::cout << "Generating a " << imageWidth << "x" << imageHeight
            << " random image..." << std::endl;

  cv::Mat myRandomImage = generateRandomImage(imageWidth, imageHeight);

  if (!myRandomImage.empty()) {
    std::cout
        << "Random image generated successfully (stored in myRandomImage)."
        << std::endl;
    // Display the image and wait for enter
    cv::imshow("Random Image", myRandomImage);
    cv::waitKey(0); // Wait for a key press indefinitely
  } else {
    std::cerr << "Failed to generate the random image." << std::endl;
    return 1;
  }

  return 0;
}