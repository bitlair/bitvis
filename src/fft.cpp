/*
 * bitvis
 * Copyright (C) Bob 2012
 * 
 * bitvis is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * bitvis is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include <stdlib.h> //TODO: REMOVE
#include <string.h>

#include "fft.h"
#include "util/timeutils.h"
#include "util/log.h"

Cfft::Cfft()
{
  m_inbuf = NULL;
  m_inbufpos = 0;
  m_fftin = NULL;
  m_outbuf = NULL;
  m_window = NULL;
  m_bufsize = 0;
  m_plan = NULL;
}

Cfft::~Cfft()
{
  Free();
}

void Cfft::Allocate(unsigned int size)
{
  if (size != m_bufsize)
  {
    Free();

    m_bufsize = size;
    m_inbuf = new float[m_bufsize];
    m_fftin = (float*)fftw_malloc(m_bufsize * sizeof(float));
    m_outbuf = (fftwf_complex*)fftw_malloc(m_bufsize * sizeof(fftwf_complex));
    m_window = new float[m_bufsize];

    //create a hamming window
    for (unsigned int i = 0; i < m_bufsize; i++)
      m_window[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (m_bufsize - 1.0f));

    Log("Building fft plan");
    int64_t start = GetTimeUs();
    m_plan = fftwf_plan_dft_r2c_1d(m_bufsize, m_fftin, m_outbuf, FFTW_MEASURE);
    Log("Build fft plan in %.0f ms", (double)(GetTimeUs() - start) / 1000.0f);
  }
}

void Cfft::Free()
{
  delete[] m_inbuf;
  fftw_free(m_fftin);
  fftw_free(m_outbuf);
  delete[] m_window;
  m_inbuf = NULL;
  m_inbufpos = 0;
  m_fftin = NULL;
  m_outbuf = NULL;
  m_window = NULL;
  m_bufsize = 0;

  if (m_plan)
  {
    fftwf_destroy_plan(m_plan);
    m_plan = NULL;
  }
}

void Cfft::ApplyWindow()
{
  float* in = m_inbuf + m_inbufpos;
  float* inend = m_inbuf + m_bufsize;
  float* out = m_fftin;
  float* window = m_window;

  while (in != inend)
    *(out++) = *(in++) * *(window++);

  in = m_inbuf;
  inend = m_inbuf + m_inbufpos;

  while (in != inend)
    *(out++) = *(in++) * *(window++);
}

void Cfft::AddSample(float sample)
{
  m_inbuf[m_inbufpos] = sample;
  m_inbufpos++;
  if (m_inbufpos == m_bufsize)
    m_inbufpos = 0;
}

