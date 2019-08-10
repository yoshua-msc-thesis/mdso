#include "../reader/MultiFovReader.h"
#include "output/CloudWriter.h"
#include "output/CloudWriterGT.h"
#include "output/DebugImageDrawer.h"
#include "output/DepthPyramidDrawer.h"
#include "output/InterpolationDrawer.h"
#include "output/TrackingDebugImageDrawer.h"
#include "output/TrajectoryWriter.h"
#include "output/TrajectoryWriterGT.h"
#include "system/DsoSystem.h"
#include "util/defs.h"
#include "util/flags.h"
#include <iostream>

DEFINE_int32(start, 1, "Number of the starting frame.");
DEFINE_int32(count, 100, "Number of frames to process.");
DEFINE_int32(gt_points, 1'000'000,
             "Number of GT points in the generated cloud.");

DEFINE_bool(gen_gt, true, "Do we need to generate GT pointcloud?");
DEFINE_bool(gen_cloud, true, "Do we need to save resulting pointcloud?");
DEFINE_bool(draw_interpolation, true,
            "Do we need to draw interpolation from initializer?");

DEFINE_bool(gen_gt_only, false, "Generate ground truth point cloud and exit.");

DEFINE_string(debug_img_dir, "output/default/debug",
              "Directory for debug images to be put into.");
DEFINE_string(trajectory_filename, "tracked_frame_to_world.txt",
              "Resulting trajectory filename (stored in 3x4 matrix form).");
DEFINE_bool(gen_gt_trajectory, true,
            "Do we need to generate ground truth trajectories?");
DEFINE_string(
    track_img_dir, "output/default/track",
    "Directory for tracking residuals on all pyr levels to be put into.");
DEFINE_bool(show_track_res, false,
            "Show tracking residuals on all levels of the pyramind?");
DEFINE_bool(show_debug_image, true,
            R"__(Show debug image? Structure of debug image:
+---+---+
| A | B |
+---+---+
| C | D |
+---+---+
Where
A displays color-coded depths projected onto the base frame;
B -- visible vs invizible OptimizedPoint-s projected onto the base frame;
C -- color-coded predicted disparities;
D -- color-coded tracking residuals on the finest pyramid level.)__");
DEFINE_string(depth_pyramid_dir, "output/default/pyr",
              "Directory for depth pyramid images to be put into.");
DEFINE_bool(draw_depth_pyramid, false,
            "Draw the depth pyramid, that is used for tracking? Will be "
            "overridden to false if write_files is set to false.");

DEFINE_bool(show_interpolation, false,
            "Show interpolated depths after initialization?");

DEFINE_bool(write_files, true,
            "Do we need to write output files into output_directory?");
DEFINE_string(output_directory, "output/default",
              "CO: \"it's dso's output directory!\"");

void readPointsInFrameGT(const MultiFovReader &reader,
                         std::vector<std::vector<Vec3>> &pointsInFrameGT,
                         std::vector<std::vector<cv::Vec3b>> &colors,
                         int maxPoints) {
  std::cout << "filling GT points..." << std::endl;
  int w = reader.cam->getWidth(), h = reader.cam->getHeight();
  int step =
      std::ceil(std::sqrt(double(FLAGS_count) * w * h / FLAGS_gt_points));
  const double maxd = 1e10;
  for (int it = FLAGS_start; it < FLAGS_start + FLAGS_count; ++it) {
    pointsInFrameGT[it].reserve((h / step) * (w / step));
    cv::Mat1d depths = reader.getDepths(it);
    cv::Mat3b frame = reader.getFrame(it);
    for (int y = 0; y < h; y += step)
      for (int x = 0; x < w; x += step) {
        Vec3 p = reader.cam->unmap(Vec2(x, y));
        p.normalize();
        double d = depths(y, x);
        if (d > maxd)
          continue;
        p *= d;
        pointsInFrameGT[it].push_back(p);
        colors[it].push_back(frame(y, x));
      }
  }
}

int main(int argc, char **argv) {
  std::string usage = "Usage: " + std::string(argv[0]) + R"abacaba( data_dir
Where data_dir names a directory with MultiFoV fishseye dataset.
It should contain "info" and "data" subdirectories.)abacaba";

  gflags::ParseCommandLineFlags(&argc, &argv, true);
  gflags::SetUsageMessage(usage);
  google::InitGoogleLogging(argv[0]);

  if (argc != 2) {
    std::cerr << "Wrong number of arguments!\n" << usage << std::endl;
    return 1;
  }

  MultiFovReader reader(argv[1]);

  if (FLAGS_gen_gt_only) {
    std::vector<std::vector<Vec3>> pointsInFrameGT(reader.getFrameCount());
    std::vector<std::vector<cv::Vec3b>> colors(reader.getFrameCount());
    readPointsInFrameGT(reader, pointsInFrameGT, colors, FLAGS_gt_points);
    std::vector<Vec3> allPoints;
    std::vector<cv::Vec3b> allColors;
    allPoints.reserve(FLAGS_gt_points);
    allColors.reserve(FLAGS_gt_points);
    for (int i = 0; i < pointsInFrameGT.size(); ++i)
      for (int j = 0; j < pointsInFrameGT[i].size(); ++j) {
        const Vec3 &p = pointsInFrameGT[i][j];
        allPoints.push_back(reader.getWorldToFrameGT(i).inverse() * p);
        allColors.push_back(colors[i][j]);
      }
    std::ofstream pointsGTOfs("pointsGT.ply");
    printInPly(pointsGTOfs, allPoints, allColors);
    return 0;
  }

  Settings settings = getFlaggedSettings();
  // settings.bundleAdjuster.fixedRotationOnSecondKF = true;

  DebugImageDrawer debugImageDrawer;
  TrackingDebugImageDrawer trackingDebugImageDrawer(
      reader.cam->camPyr(settings.pyramid.levelNum), settings.frameTracker,
      settings.pyramid);
  TrajectoryWriter trajectoryWriter(FLAGS_output_directory, "tracked_pos.txt",
                                    FLAGS_trajectory_filename);
  TrajectoryWriterGT trajectoryWriterGT(
      reader.getAllWorldToFrameGT(), FLAGS_output_directory,
      "ground_truth_pos.txt", "matrix_form_GT_pose.txt");
  std::unique_ptr<CloudWriter> cloudWriter;
  if (FLAGS_gen_cloud)
    cloudWriter = std::unique_ptr<CloudWriter>(new CloudWriter(
        reader.cam.get(), FLAGS_output_directory, "points.ply"));

  std::unique_ptr<CloudWriterGT> cloudWriterGTPtr;
  if (FLAGS_gen_gt) {
    std::vector<std::vector<Vec3>> pointsInFrameGT(reader.getFrameCount());
    std::vector<std::vector<cv::Vec3b>> colors(reader.getFrameCount());
    readPointsInFrameGT(reader, pointsInFrameGT, colors, FLAGS_gt_points);
    cloudWriterGTPtr.reset(
        new CloudWriterGT(reader.getAllWorldToFrameGT(), pointsInFrameGT,
                          colors, FLAGS_output_directory, "pointsGT.ply"));
  }

  InterpolationDrawer interpolationDrawer(reader.cam.get());

  DepthPyramidDrawer depthPyramidDrawer;

  Observers observers;
  if (FLAGS_write_files || FLAGS_show_debug_image)
    observers.dso.push_back(&debugImageDrawer);
  observers.dso.push_back(&trajectoryWriter);
  if (FLAGS_gen_gt_trajectory)
    observers.dso.push_back(&trajectoryWriterGT);
  if (FLAGS_gen_cloud)
    observers.dso.push_back(cloudWriter.get());
  if (FLAGS_write_files && FLAGS_draw_depth_pyramid)
    observers.frameTracker.push_back(&depthPyramidDrawer);
  if (cloudWriterGTPtr)
    observers.dso.push_back(cloudWriterGTPtr.get());
  if (FLAGS_write_files || FLAGS_show_track_res)
    observers.frameTracker.push_back(&trackingDebugImageDrawer);
  if (FLAGS_draw_interpolation)
    observers.initializer.push_back(&interpolationDrawer);

  std::cout << "running DSO.." << std::endl;
  DsoSystem dso(reader.cam.get(), observers, settings);
  for (int it = FLAGS_start; it < FLAGS_start + FLAGS_count; ++it) {
    std::cout << "add frame #" << it << std::endl;
    dso.addFrame(reader.getFrame(it), it);

    if (interpolationDrawer.didInitialize()) {
      cv::Mat3b interpolation = interpolationDrawer.draw();
      if (FLAGS_write_files)
        cv::imwrite(fileInDir(FLAGS_output_directory, "interpolation.jpg"),
                    interpolation);
      if (FLAGS_show_interpolation) {
        cv::imshow("interpolation", interpolation);
        cv::waitKey();
      }
    }

    if (FLAGS_write_files) {
      cv::Mat3b debugImage = debugImageDrawer.draw();
      cv::imwrite(fileInDir(FLAGS_debug_img_dir,
                            "frame#" + std::to_string(it) + ".jpg"),
                  debugImage);
      cv::Mat3b trackImage = trackingDebugImageDrawer.drawAllLevels();
      cv::imwrite(fileInDir(FLAGS_track_img_dir,
                            "frame#" + std::to_string(it) + ".jpg"),
                  trackImage);
      if (FLAGS_draw_depth_pyramid && depthPyramidDrawer.pyrChanged()) {
        cv::Mat pyrImage = depthPyramidDrawer.getLastPyr();
        cv::imwrite(fileInDir(FLAGS_depth_pyramid_dir,
                              "frame#" + std::to_string(it) + ".jpg"),
                    pyrImage);
      }
    }
    if (FLAGS_show_debug_image)
      cv::imshow("debug", debugImageDrawer.draw());
    if (FLAGS_show_track_res)
      cv::imshow("tracking", trackingDebugImageDrawer.drawAllLevels());
    if (FLAGS_show_debug_image || FLAGS_show_track_res)
      cv::waitKey(1);
  }

  return 0;
}
