#include <iostream>
#include <vector>
#include <chrono> // Required for timing
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp> // Include if you want to save images later

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
    // Error message moved to main, return empty Mat for checking
    return cv::Mat(); // Return an empty Mat to indicate failure
  }

  // Create an 8-bit 3-channel BGR image (Height x Width)
  cv::Mat randomImage(height, width, CV_8UC3);

  // Fill the image with random values (0-255 for each channel)
  // Use thread_local RNG for potentially better performance in multithreaded contexts,
  // though not strictly necessary here.
  cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));

  return randomImage;
}

int main(int argc, char **argv) {
  int imageWidth = 640;
  int imageHeight = 480;
  const int numImages = 50; // Number of images to generate

  std::cout << "Attempting to generate " << numImages << " random images ("
            << imageWidth << "x" << imageHeight << ")..." << std::endl;

  // --- Time Generation Only ---
  auto start_gen_only = std::chrono::high_resolution_clock::now();
  bool generation_ok = true;
  for (int i = 0; i < numImages; ++i) {
    cv::Mat tempImg = generateRandomImage(imageWidth, imageHeight);
    if (tempImg.empty()) {
        std::cerr << "Error: Image dimensions must be positive (" << imageWidth << "x" << imageHeight <<"). Aborting." << std::endl;
        generation_ok = false;
        break;
    }
  }
  auto end_gen_only = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> generation_only_duration = end_gen_only - start_gen_only;

  if (!generation_ok) {
      return 1; // Exit if generation failed
  }


  // --- Time Generation and Storage ---
  std::vector<cv::Mat> allImages;
  allImages.reserve(numImages); // Pre-allocate vector capacity for efficiency

  auto start_gen_store = std::chrono::high_resolution_clock::now();
  bool generation_store_ok = true;
  for (int i = 0; i < numImages; ++i) {
    cv::Mat img = generateRandomImage(imageWidth, imageHeight);
     if (img.empty()) {
        // This check might be redundant if the first loop passed, but good practice
        std::cerr << "Error during generation and storage: Image dimensions must be positive." << std::endl;
        generation_store_ok = false;
        break;
    }
    // Use std::move to potentially avoid copying data if the Mat implementation allows
    allImages.push_back(std::move(img));
  }
  auto end_gen_store = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> generation_and_storage_duration = end_gen_store - start_gen_store;

   if (!generation_store_ok) {
      return 1; // Exit if generation during storage failed
  }

  // --- Calculate and Print Times ---
  // Note: Storage time is estimated by subtraction. It includes vector allocation/push_back overhead.
  std::chrono::duration<double, std::milli> estimated_storage_duration = generation_and_storage_duration - generation_only_duration;
  // Ensure storage time isn't negative due to timing fluctuations
  if (estimated_storage_duration.count() < 0) {
      estimated_storage_duration = std::chrono::duration<double, std::milli>(0);
  }

  std::cout << "\n--- Timing Results ---" << std::endl;
  std::cout << "Time to generate " << numImages << " images (generation only): "
            << generation_only_duration.count() << " ms" << std::endl;
  std::cout << "Estimated time to store " << numImages << " images in vector: "
            << estimated_storage_duration.count() << " ms" << std::endl;
  std::cout << "Total time for generation and storage: "
            << generation_and_storage_duration.count() << " ms" << std::endl;
  std::cout << "----------------------\n" << std::endl;


  // --- Display the last generated image ---
  if (!allImages.empty()) {
    std::cout << "Displaying the last generated image (" << numImages << "/" << numImages <<"). Press any key in the window to exit." << std::endl;
    cv::imshow("Last Random Image", allImages.back());
    cv::waitKey(0); // Wait for a key press indefinitely
  } else {
      // Should not happen if checks above worked, but included for safety
      std::cerr << "No images were generated or stored successfully." << std::endl;
      return 1;
  }


  return 0;
}
