/*
 * SplashBackgroundRenderer.cc
 *
 * Copyright (C) 2012 Lu Wang <coolwanglu@gmail.com>
 */

#include <fstream>
#include <vector>

#include <PDFDoc.h>
#include <goo/PNGWriter.h>

#include "Base64Stream.h"

#include "SplashBackgroundRenderer.h"

namespace pdf2htmlEX {

using std::string;
using std::ifstream;
using std::vector;

const SplashColor SplashBackgroundRenderer::white = {255,255,255};

/*
 * SplashOutputDev::startPage would paint the whole page with the background color
 * And thus have modified region set to the whole page area
 * We do not want that.
 */
void SplashBackgroundRenderer::startPage(int pageNum, GfxState *state, XRef *xrefA)
{
    SplashOutputDev::startPage(pageNum, state, xrefA);
    clearModRegion();
}

void SplashBackgroundRenderer::drawChar(GfxState *state, double x, double y,
  double dx, double dy,
  double originX, double originY,
  CharCode code, int nBytes, Unicode *u, int uLen)
{
    // draw characters as image when
    // - in fallback mode
    // - OR there is special filling method
    // - OR using a writing mode font
    // - OR using a Type 3 font
    if((param.fallback)
       || ( (state->getFont()) 
            && ( (state->getFont()->getWMode())
                 || (state->getFont()->getType() == fontType3)
               )
          )
      )
    {
        SplashOutputDev::drawChar(state,x,y,dx,dy,originX,originY,code,nBytes,u,uLen);
    }
}

static GBool annot_cb(Annot *, void *) {
    return false;
};

void SplashBackgroundRenderer::render_page(PDFDoc * doc, int pageno)
{
    doc->displayPage(this, pageno, param.h_dpi, param.v_dpi,
            0, 
            (!(param.use_cropbox)),
            false, false,
            nullptr, nullptr, &annot_cb, nullptr);
}

void SplashBackgroundRenderer::embed_image(int pageno)
{
    // xmin->xmax is top->bottom
    int xmin, xmax, ymin, ymax;
    getModRegion(&xmin, &ymin, &xmax, &ymax);

    // dump the background image only when it is not empty
    if((xmin <= xmax) && (ymin <= ymax))
    {
        {
            auto fn = html_renderer->str_fmt("%s/bg%x.png", (param.embed_image ? param.tmp_dir : param.dest_dir).c_str(), pageno);
            if(param.embed_image)
                html_renderer->tmp_files.add((char*)fn);

            dump_image((char*)fn, xmin, ymin, xmax, ymax);
        }

        double h_scale = html_renderer->text_zoom_factor() * DEFAULT_DPI / param.h_dpi;
        double v_scale = html_renderer->text_zoom_factor() * DEFAULT_DPI / param.v_dpi;

        auto & f_page = *(html_renderer->f_curpage);
        auto & all_manager = html_renderer->all_manager;
        
        f_page << "<img class=\"" << CSS::BACKGROUND_IMAGE_CN 
            << " " << CSS::LEFT_CN      << all_manager.left.install(((double)xmin) * h_scale)
            << " " << CSS::BOTTOM_CN    << all_manager.bottom.install(((double)getBitmapHeight() - 1 - ymax) * v_scale)
            << " " << CSS::WIDTH_CN     << all_manager.width.install(((double)(xmax - xmin + 1)) * h_scale)
            << " " << CSS::HEIGHT_CN    << all_manager.height.install(((double)(ymax - ymin + 1)) * v_scale)
            << "\" alt=\"\" src=\"";

        if(param.embed_image)
        {
            auto path = html_renderer->str_fmt("%s/bg%x.png", param.tmp_dir.c_str(), pageno);
            ifstream fin((char*)path, ifstream::binary);
            if(!fin)
                throw string("Cannot read background image ") + (char*)path;
            f_page << "data:image/png;base64," << Base64Stream(fin);
        }
        else
        {
            f_page << (char*)html_renderer->str_fmt("bg%x.png", pageno);
        }
        f_page << "\"/>";
    }
}

// There will be mem leak when exception is thrown !
void SplashBackgroundRenderer::dump_image(const char * filename, int x1, int y1, int x2, int y2)
{
    int width = x2 - x1 + 1;
    int height = y2 - y1 + 1;
    if((width <= 0) || (height <= 0))
        throw "Bad metric for background image";

    FILE * f = fopen(filename, "wb");
    if(!f)
        throw string("Cannot open file for background image " ) + filename;

    ImgWriter * writer = new PNGWriter();
    if(!writer->init(f, width, height, param.h_dpi, param.v_dpi))
        throw "Cannot initialize PNGWriter";
        
    auto * bitmap = getBitmap();
    assert(bitmap->getMode() == splashModeRGB8);

    SplashColorPtr data = bitmap->getDataPtr();
    int row_size = bitmap->getRowSize();

    vector<unsigned char*> pointers;
    pointers.reserve(height);
    SplashColorPtr p = data + y1 * row_size + x1 * 3;
    for(int i = 0; i < height; ++i)
    {
        pointers.push_back(p);
        p += row_size;
    }
    
    if(!writer->writePointers(pointers.data(), height)) {
        throw "Cannot write background image";
    }

    delete writer;
    fclose(f);
}

} // namespace pdf2htmlEX
