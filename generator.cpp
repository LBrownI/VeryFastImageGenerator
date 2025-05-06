// random_image_generator.cpp
#include <iostream>
#include <string>
#include <vector>
#include <chrono>       // For timing
#include <iomanip>      // For std::fixed and std::setprecision
#include <filesystem>   // For directory creation (C++17)
#include <pthread.h>    // For threading
#include <stdexcept>    // For std::stoi, std::stod exceptions
#include <thread>       // For std::this_thread::sleep_for
#include <algorithm>    // For std::tolower

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp> // Specifically for cv::imwrite

namespace fs = std::filesystem;

// Structure to hold arguments for the image processing thread
struct ThreadArgs {
    int width;
    int height;
    int totalImages;
    double fps; // Frames per second
    std::string image_extension;
    std::string output_directory;
};

// Function to generate a random image
cv::Mat generateRandomImage(int width, int height) {
    if (width <= 0 || height <= 0) {
        std::cerr << "Error: Image dimensions must be positive." << std::endl;
        return cv::Mat(); // Return an empty Mat on error
    }
    cv::Mat randomImage(height, width, CV_8UC3);
    cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    return randomImage;
}

// Thread function to generate and save images, simulating FPS
void* imageProcessingLoop(void* arg) {
    ThreadArgs* args = static_cast<ThreadArgs*>(arg);

    double total_generation_time_sec = 0.0;
    double total_saving_time_sec = 0.0;
    int images_successfully_saved = 0;

    // Calculate target frame duration if FPS is positive
    std::chrono::duration<double> target_frame_duration(0);
    if (args->fps > 0) {
        target_frame_duration = std::chrono::duration<double>(1.0 / args->fps);
    }

    auto overall_thread_start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < args->totalImages; ++i) {
        auto iteration_start_time = std::chrono::high_resolution_clock::now();

        // --- Image Generation ---
        auto gen_start_time = std::chrono::high_resolution_clock::now();
        cv::Mat image = generateRandomImage(args->width, args->height);
        auto gen_end_time = std::chrono::high_resolution_clock::now();
        total_generation_time_sec += std::chrono::duration<double>(gen_end_time - gen_start_time).count();

        if (image.empty()) {
            std::cerr << "Error: Failed to generate image " << i << "." << std::endl;
            if (args->fps > 0) {
                 auto iteration_end_time = std::chrono::high_resolution_clock::now();
                 auto iteration_duration = iteration_end_time - iteration_start_time;
                 if (iteration_duration < target_frame_duration) {
                    std::this_thread::sleep_for(target_frame_duration - iteration_duration);
                 }
            }
            continue; 
        }

        // --- Image Saving ---
        std::string filename = args->output_directory + "/image_" + std::to_string(i) + "." + args->image_extension;
        
        auto save_start_time = std::chrono::high_resolution_clock::now();
        bool saved_successfully = false;
        try {
            saved_successfully = cv::imwrite(filename, image);
        } catch (const cv::Exception& ex) {
            std::cerr << "OpenCV error while saving image " << filename << ": " << ex.what() << std::endl;
        }
        auto save_end_time = std::chrono::high_resolution_clock::now();

        if (saved_successfully) {
            total_saving_time_sec += std::chrono::duration<double>(save_end_time - save_start_time).count();
            images_successfully_saved++;
        } else {
            std::cerr << "Error: Failed to save image " << filename << std::endl;
        }

        // --- FPS Simulation ---
        if (args->fps > 0) {
            auto iteration_end_time = std::chrono::high_resolution_clock::now();
            auto processing_duration = std::chrono::duration<double>(iteration_end_time - iteration_start_time);
            
            if (processing_duration < target_frame_duration) {
                std::this_thread::sleep_for(target_frame_duration - processing_duration);
            }
        }
    }

    auto overall_thread_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> overall_thread_elapsed_seconds = overall_thread_end_time - overall_thread_start_time;

    // Print summary
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n--- Image Processing Thread Summary ---" << std::endl;
    std::cout << "Requested images to generate: " << args->totalImages << std::endl;
    std::cout << "Target FPS: " << (args->fps > 0 ? std::to_string(args->fps) : "Max (no limit)") << std::endl;
    std::cout << "Output format: ." << args->image_extension << std::endl;
    std::cout << "Images successfully generated and saved: " << images_successfully_saved << std::endl;
    std::cout << "Total time for image generation (pure): " << total_generation_time_sec << " seconds." << std::endl;
    std::cout << "Total time for image saving (pure): " << total_saving_time_sec << " seconds." << std::endl;
    std::cout << "Total operational time in thread (including FPS delays): " << overall_thread_elapsed_seconds.count() << " seconds." << std::endl;
    
    if (images_successfully_saved > 0) {
        std::cout << "Average time per image (generation): " << total_generation_time_sec / images_successfully_saved << " seconds." << std::endl;
        std::cout << "Average time per image (saving): " << total_saving_time_sec / images_successfully_saved << " seconds." << std::endl;
        if (overall_thread_elapsed_seconds.count() > 0) {
             std::cout << "Effective FPS (based on total thread time): " << images_successfully_saved / overall_thread_elapsed_seconds.count() << " FPS." << std::endl;
        }
    }
    std::cout << "-------------------------------------\n" << std::endl;

    pthread_exit(nullptr);
    return nullptr; 
}

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <width> <height> <num_images> <fps> <extension>" << std::endl;
    std::cerr << "  width:         Image width in pixels (integer > 0)" << std::endl;
    std::cerr << "  height:        Image height in pixels (integer > 0)" << std::endl;
    std::cerr << "  num_images:    Total number of images to generate (integer > 0)" << std::endl;
    std::cerr << "  fps:           Target frames per second (double, e.g., 30.0. Use 0 for max speed)" << std::endl;
    std::cerr << "  extension:     Image file extension (e.g., png, jpg, bmp, tiff)" << std::endl;
    std::cerr << "Example: " << program_name << " 1920 1080 100 30 png" << std::endl;
}

// Function to convert string to lowercase
std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

int main(int argc, char *argv[]) {
    if (argc != 6) { // program_name + width + height + num_images + fps + extension
        std::cerr << "Error: Incorrect number of arguments. Expected 5, got " << argc - 1 << "." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    ThreadArgs args;
    args.output_directory = "generated_images"; // Default output directory

    try {
        args.width = std::stoi(argv[1]);
        args.height = std::stoi(argv[2]);
        args.totalImages = std::stoi(argv[3]);
        args.fps = std::stod(argv[4]); // FPS can be a double
        args.image_extension = to_lower(argv[5]);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Error: Invalid argument type. Width, height, num_images must be integers. FPS must be a number." << std::endl;
        std::cerr << "Details: " << ia.what() << std::endl;
        print_usage(argv[0]);
        return 1;
    } catch (const std::out_of_range& oor) {
        std::cerr << "Error: Argument value out of range." << std::endl;
        std::cerr << "Details: " << oor.what() << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // --- Input Validation ---
    if (args.width <= 0) {
        std::cerr << "Error: Image width must be a positive integer. Received: " << args.width << std::endl; return 1;
    }
    if (args.height <= 0) {
        std::cerr << "Error: Image height must be a positive integer. Received: " << args.height << std::endl; return 1;
    }
    if (args.totalImages <= 0) {
        std::cerr << "Error: Number of images must be a positive integer. Received: " << args.totalImages << std::endl; return 1;
    }
    if (args.fps < 0) { // Allow 0 for max speed
        std::cerr << "Error: FPS cannot be negative. Use 0 for maximum speed. Received: " << args.fps << std::endl; return 1;
    }

    // Validate extension
    const std::vector<std::string> supported_extensions = {"png", "jpg", "jpeg", "bmp", "tiff", "tif"};
    bool valid_extension = false;
    for (const auto& ext : supported_extensions) {
        if (args.image_extension == ext) {
            valid_extension = true;
            break;
        }
    }
    if (!valid_extension) {
        std::cerr << "Error: Unsupported or invalid image extension: '" << args.image_extension << "'." << std::endl;
        std::cerr << "Supported extensions are: png, jpg, jpeg, bmp, tiff." << std::endl;
        return 1;
    }

    // --- Directory Creation ---
    if (!fs::exists(args.output_directory)) {
        std::cout << "Output directory '" << args.output_directory << "' does not exist. Attempting to create it..." << std::endl;
        try {
            if (fs::create_directories(args.output_directory)) {
                std::cout << "Successfully created directory: " << args.output_directory << std::endl;
            } else {
                std::cerr << "Error: Could not create directory " << args.output_directory << "." << std::endl; return 1;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Filesystem error while creating directory " << args.output_directory << ": " << e.what() << std::endl; return 1;
        }
    } else {
        std::cout << "Output directory '" << args.output_directory << "' already exists. Images will be saved there." << std::endl;
    }

    // --- Threading and Execution ---
    pthread_t threadID; 
    auto main_op_start_time = std::chrono::high_resolution_clock::now();

    std::cout << "Starting image generation process..." << std::endl;
    std::cout << "Configuration: " 
              << args.width << "x" << args.height << " images, "
              << args.totalImages << " total, "
              << (args.fps > 0 ? std::to_string(args.fps) + " FPS, " : "Max FPS, ")
              << "." << args.image_extension << " format, "
              << "output to '" << args.output_directory << "'" << std::endl;

    if (pthread_create(&threadID, nullptr, imageProcessingLoop, &args)) {
        std::cerr << "Error: Failed to create the image processing thread." << std::endl;
        return 1;
    }

    pthread_join(threadID, nullptr); 

    auto main_op_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> main_op_elapsed_seconds = main_op_end_time - main_op_start_time;

    std::cout << "Image processing thread has completed." << std::endl;
    std::cout << "Total execution time of main program (including thread): "
              << std::fixed << std::setprecision(3) << main_op_elapsed_seconds.count() << " seconds." << std::endl;

    return 0;
}

