//-*-C++-*-

#ifndef PDF_TEXT_LAYOUT_H
#define PDF_TEXT_LAYOUT_H

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace pdflib
{
  // Lays text runs out on a character grid, so that the plain text keeps the
  // page's columns, indentation and vertical spacing. This is the fixed-pitch
  // approximation pypdf calls layout mode: one column is the page's typical
  // character width, one row its typical line height.
  class pdf_text_layout
  {
  public:

    typedef std::array<double, 4> box_type; // x0, y0, x1, y1; y grows upwards

    // `texts[i]` occupies `boxes[i]` and is `lengths[i]` characters long.
    static std::string render(const std::vector<std::string>& texts,
                              const std::vector<box_type>& boxes,
                              const std::vector<int>& lengths);

  private:

    // More blank lines than this between two rows say nothing more about the
    // layout and only pad the output.
    static const int MAX_BLANK_LINES = 3;

    static double median(std::vector<double> values, double fallback);
  };

  inline double pdf_text_layout::median(std::vector<double> values, double fallback)
  {
    if(values.empty())
      {
        return fallback;
      }

    std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());

    return values[middle];
  }

  inline std::string pdf_text_layout::render(const std::vector<std::string>& texts,
                                             const std::vector<box_type>& boxes,
                                             const std::vector<int>& lengths)
  {
    std::size_t number_of_runs = texts.size();
    if(number_of_runs == 0)
      {
        return "";
      }

    std::vector<double> char_widths;
    std::vector<double> line_heights;
    double left = boxes[0][0];

    for(std::size_t index = 0; index < number_of_runs; index++)
      {
        double width = boxes[index][2] - boxes[index][0];
        double height = boxes[index][3] - boxes[index][1];

        if(lengths[index] > 0 and width > 0.0)
          {
            char_widths.push_back(width / lengths[index]);
          }
        if(height > 0.0)
          {
            line_heights.push_back(height);
          }

        left = std::min(left, boxes[index][0]);
      }

    // 6 x 12 points: a 10-point fixed-pitch face, when the page gives no measure
    double char_width = median(char_widths, 6.0);
    double line_height = median(line_heights, 12.0);

    // top to bottom, then left to right
    std::vector<std::size_t> order(number_of_runs);
    for(std::size_t index = 0; index < number_of_runs; index++)
      {
        order[index] = index;
      }

    auto center = [&boxes](std::size_t index) {
      return 0.5 * (boxes[index][1] + boxes[index][3]);
    };

    std::stable_sort(order.begin(), order.end(), [&center](std::size_t lhs, std::size_t rhs) {
      return center(lhs) > center(rhs);
    });

    // Runs whose centers are within half a line of the row's first run share
    // the row.
    std::vector<std::vector<std::size_t> > rows;
    std::vector<double> row_centers;

    for(std::size_t index : order)
      {
        if(rows.empty() or (row_centers.back() - center(index)) > 0.5 * line_height)
          {
            rows.push_back({});
            row_centers.push_back(center(index));
          }
        rows.back().push_back(index);
      }

    std::string result;

    for(std::size_t row = 0; row < rows.size(); row++)
      {
        if(row > 0)
          {
            int blank_lines = static_cast<int>(std::lround((row_centers[row - 1] - row_centers[row]) / line_height)) - 1;
            blank_lines = std::max(0, std::min(blank_lines, MAX_BLANK_LINES));

            result += std::string(static_cast<std::size_t>(blank_lines) + 1, '\n');
          }

        std::vector<std::size_t>& runs = rows[row];
        std::stable_sort(runs.begin(), runs.end(), [&boxes](std::size_t lhs, std::size_t rhs) {
          return boxes[lhs][0] < boxes[rhs][0];
        });

        int cursor = 0; // characters written on this row so far
        for(std::size_t index : runs)
          {
            int column = static_cast<int>(std::lround((boxes[index][0] - left) / char_width));

            // Runs that the grid would overlap still stay apart by one space.
            int padding = (column > cursor) ? (column - cursor) : ((cursor > 0) ? 1 : 0);

            result += std::string(static_cast<std::size_t>(padding), ' ');
            result += texts[index];

            cursor += padding + lengths[index];
          }
      }

    return result;
  }

}

#endif
