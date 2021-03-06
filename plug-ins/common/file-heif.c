/*
 * GIMP HEIF loader / write plugin.
 * Copyright (c) 2018 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <libheif/heif.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define LOAD_PROC      "file-heif-load"
#define SAVE_PROC      "file-heif-save"
#define PLUG_IN_BINARY "file-heif"


typedef struct _Heif      Heif;
typedef struct _HeifClass HeifClass;

struct _Heif
{
  GimpPlugIn      parent_instance;
};

struct _HeifClass
{
  GimpPlugInClass parent_class;
};


#define HEIF_TYPE  (heif_get_type ())
#define HEIF (obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), HEIF_TYPE, Heif))

GType                   heif_get_type         (void) G_GNUC_CONST;

static GList          * heif_query_procedures (GimpPlugIn           *plug_in);
static GimpProcedure  * heif_create_procedure (GimpPlugIn           *plug_in,
                                               const gchar          *name);

static GimpValueArray * heif_load             (GimpProcedure        *procedure,
                                               GimpRunMode           run_mode,
                                               GFile                *file,
                                               const GimpValueArray *args,
                                               gpointer              run_data);
static GimpValueArray * heif_save             (GimpProcedure        *procedure,
                                               GimpRunMode           run_mode,
                                               GimpImage            *image,
                                               gint                  n_drawables,
                                               GimpDrawable        **drawables,
                                               GFile                *file,
                                               const GimpValueArray *args,
                                               gpointer              run_data);

static GimpImage      * load_image            (GFile               *file,
                                               gboolean             interactive,
                                               GimpPDBStatusType    *status,
                                               GError              **error);
static gboolean         save_image            (GFile                *file,
                                               GimpImage            *image,
                                               GimpDrawable         *drawable,
                                               GObject              *config,
                                               GError              **error);

static gboolean         load_dialog           (struct heif_context  *heif,
                                               uint32_t             *selected_image);
static gboolean         save_dialog           (GimpProcedure        *procedure,
                                               GObject              *config);


G_DEFINE_TYPE (Heif, heif, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (HEIF_TYPE)


static void
heif_class_init (HeifClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = heif_query_procedures;
  plug_in_class->create_procedure = heif_create_procedure;
}

static void
heif_init (Heif *heif)
{
}

static GList *
heif_query_procedures (GimpPlugIn *plug_in)
{
  GList *list = NULL;

  list = g_list_append (list, g_strdup (LOAD_PROC));
  list = g_list_append (list, g_strdup (SAVE_PROC));

  return list;
}

static GimpProcedure *
heif_create_procedure (GimpPlugIn  *plug_in,
                      const gchar *name)
{
  GimpProcedure *procedure = NULL;

  if (! strcmp (name, LOAD_PROC))
    {
      procedure = gimp_load_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           heif_load, NULL, NULL);

      gimp_procedure_set_menu_label (procedure, N_("HEIF/HEIC"));

      gimp_procedure_set_documentation (procedure,
                                        _("Loads HEIF images"),
                                        _("Load image stored in HEIF format (High "
                                          "Efficiency Image File Format). Typical "
                                          "suffices for HEIF files are .heif, "
                                          ".heic."),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Dirk Farin <farin@struktur.de>",
                                      "Dirk Farin <farin@struktur.de>",
                                      "2018");

      gimp_file_procedure_set_handles_remote (GIMP_FILE_PROCEDURE (procedure),
                                              TRUE);
      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/heif");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "heif,heic");

      /* HEIF is an ISOBMFF format whose "brand" (the value after "ftyp")
       * can be of various values.
       * See also: https://gitlab.gnome.org/GNOME/gimp/issues/2209
       */
      gimp_file_procedure_set_magics (GIMP_FILE_PROCEDURE (procedure),
                                      "4,string,ftypheic,4,string,ftypheix,"
                                      "4,string,ftyphevc,4,string,ftypheim,"
                                      "4,string,ftypheis,4,string,ftyphevm,"
                                      "4,string,ftyphevs,4,string,ftypmif1,"
                                      "4,string,ftypmsf1");
    }
  else if (! strcmp (name, SAVE_PROC))
    {
      procedure = gimp_save_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           heif_save, NULL, NULL);

      gimp_procedure_set_image_types (procedure, "RGB*");

      gimp_procedure_set_menu_label (procedure, N_("HEIF/HEIC"));

      gimp_procedure_set_documentation (procedure,
                                        _("Exports HEIF images"),
                                        _("Save image in HEIF format (High "
                                          "Efficiency Image File Format)."),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Dirk Farin <farin@struktur.de>",
                                      "Dirk Farin <farin@struktur.de>",
                                      "2018");

      gimp_file_procedure_set_handles_remote (GIMP_FILE_PROCEDURE (procedure),
                                              TRUE);
      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/heif");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "heif,heic");

      GIMP_PROC_ARG_INT (procedure, "quality",
                         "Quality",
                         "Quality factor (0 = worst, 100 = best)",
                         0, 100, 50,
                         G_PARAM_READWRITE);

      GIMP_PROC_ARG_BOOLEAN (procedure, "lossless",
                             "Lossless",
                             "Use lossless compression",
                             FALSE,
                             G_PARAM_READWRITE);

      GIMP_PROC_AUX_ARG_BOOLEAN (procedure, "save-color-profile",
                                 "Save color profile",
                                 "Save the image's color profile",
                                 gimp_export_color_profile (),
                                 G_PARAM_READWRITE);
    }

  return procedure;
}

static GimpValueArray *
heif_load (GimpProcedure        *procedure,
          GimpRunMode           run_mode,
          GFile                *file,
          const GimpValueArray *args,
          gpointer              run_data)
{
  GimpValueArray    *return_vals;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GimpImage         *image;
  gboolean           interactive;
  GError            *error = NULL;

  INIT_I18N ();
  gegl_init (NULL, NULL);

  interactive = (run_mode == GIMP_RUN_INTERACTIVE);

  if (interactive)
    gimp_ui_init (PLUG_IN_BINARY);

  image = load_image (file, interactive, &status, &error);

  if (! image)
    return gimp_procedure_new_return_values (procedure, status, error);

  return_vals = gimp_procedure_new_return_values (procedure,
                                                  GIMP_PDB_SUCCESS,
                                                  NULL);

  GIMP_VALUES_SET_IMAGE (return_vals, 1, image);

  return return_vals;
}

static GimpValueArray *
heif_save (GimpProcedure        *procedure,
          GimpRunMode           run_mode,
          GimpImage            *image,
          gint                  n_drawables,
          GimpDrawable        **drawables,
          GFile                *file,
          const GimpValueArray *args,
          gpointer              run_data)
{
  GimpProcedureConfig *config;
  GimpPDBStatusType    status = GIMP_PDB_SUCCESS;
  GimpExportReturn     export = GIMP_EXPORT_CANCEL;
  GError              *error  = NULL;

  INIT_I18N ();
  gegl_init (NULL, NULL);

  config = gimp_procedure_create_config (procedure);
  gimp_procedure_config_begin_export (config, image, run_mode,
                                      args, "image/heif");

  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
    case GIMP_RUN_WITH_LAST_VALS:
      gimp_ui_init (PLUG_IN_BINARY);

      export = gimp_export_image (&image, &n_drawables, &drawables, "HEIF",
                                  GIMP_EXPORT_CAN_HANDLE_RGB |
                                  GIMP_EXPORT_CAN_HANDLE_ALPHA);

      if (export == GIMP_EXPORT_CANCEL)
        return gimp_procedure_new_return_values (procedure,
                                                 GIMP_PDB_CANCEL,
                                                 NULL);
      break;

    default:
      break;
    }

  if (n_drawables != 1)
    {
      g_set_error (&error, G_FILE_ERROR, 0,
                   _("HEIF format does not support multiple layers."));

      return gimp_procedure_new_return_values (procedure,
                                               GIMP_PDB_CALLING_ERROR,
                                               error);
    }

  if (run_mode == GIMP_RUN_INTERACTIVE)
    {
      if (! save_dialog (procedure, G_OBJECT (config)))
        status = GIMP_PDB_CANCEL;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      if (! save_image (file, image, drawables[0], G_OBJECT (config),
                        &error))
        {
          status = GIMP_PDB_EXECUTION_ERROR;
        }
    }

  gimp_procedure_config_end_export (config, image, file, status);
  g_object_unref (config);

  if (export == GIMP_EXPORT_EXPORT)
    {
      gimp_image_delete (image);
      g_free (drawables);
    }

  return gimp_procedure_new_return_values (procedure, status, error);
}

static goffset
get_file_size (GFile   *file,
               GError **error)
{
  GFileInfo *info;
  goffset    size = 1;

  info = g_file_query_info (file,
                            G_FILE_ATTRIBUTE_STANDARD_SIZE,
                            G_FILE_QUERY_INFO_NONE,
                            NULL, error);
  if (info)
    {
      size = g_file_info_get_size (info);

      g_object_unref (info);
    }

  return size;
}

GimpImage *
load_image (GFile              *file,
            gboolean            interactive,
            GimpPDBStatusType  *status,
            GError            **error)
{
  GInputStream             *input;
  goffset                   file_size;
  guchar                   *file_buffer;
  gsize                     bytes_read;
  struct heif_context      *ctx;
  struct heif_error         err;
  struct heif_image_handle *handle  = NULL;
  struct heif_image        *img     = NULL;
  GimpColorProfile         *profile = NULL;
  gint                      n_images;
  heif_item_id              primary;
  heif_item_id              selected_image;
  gboolean                  has_alpha;
  gint                      width;
  gint                      height;
  GimpImage                *image;
  GimpLayer                *layer;
  GeglBuffer               *buffer;
  const Babl               *format;
  const guint8             *data;
  gint                      stride;

  gimp_progress_init_printf (_("Opening '%s'"),
                             g_file_get_parse_name (file));

  file_size = get_file_size (file, error);
  if (file_size <= 0)
    return NULL;

  input = G_INPUT_STREAM (g_file_read (file, NULL, error));
  if (! input)
    return NULL;

  file_buffer = g_malloc (file_size);

  if (! g_input_stream_read_all (input, file_buffer, file_size,
                                 &bytes_read, NULL, error) &&
      bytes_read == 0)
    {
      g_free (file_buffer);
      g_object_unref (input);
      return NULL;
    }

  gimp_progress_update (0.25);

  ctx = heif_context_alloc ();

  err = heif_context_read_from_memory (ctx, file_buffer, file_size, NULL);
  if (err.code)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Loading HEIF image failed: %s"),
                   err.message);
      heif_context_free (ctx);
      g_free (file_buffer);
      g_object_unref (input);

      return NULL;
    }

  g_free (file_buffer);
  g_object_unref (input);

  gimp_progress_update (0.5);

  /* analyze image content
   * Is there more than one image? Which image is the primary image?
   */

  n_images = heif_context_get_number_of_top_level_images (ctx);
  if (n_images == 0)
    {
      g_set_error_literal (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                           _("Loading HEIF image failed: "
                             "Input file contains no readable images"));
      heif_context_free (ctx);

      return NULL;
    }

  err = heif_context_get_primary_image_ID (ctx, &primary);
  if (err.code)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Loading HEIF image failed: %s"),
                   err.message);
      heif_context_free (ctx);

      return NULL;
    }

  /* if primary image is no top level image or not present (invalid
   * file), just take the first image
   */

  if (! heif_context_is_top_level_image_ID (ctx, primary))
    {
      gint n = heif_context_get_list_of_top_level_image_IDs (ctx, &primary, 1);
      g_assert (n == 1);
    }

  selected_image = primary;

  /* if there are several images in the file and we are running
   * interactive, let the user choose a picture
   */

  if (interactive && n_images > 1)
    {
      if (! load_dialog (ctx, &selected_image))
        {
          heif_context_free (ctx);

          *status = GIMP_PDB_CANCEL;

          return NULL;
        }
    }

  /* load the picture */

  err = heif_context_get_image_handle (ctx, selected_image, &handle);
  if (err.code)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Loading HEIF image failed: %s"),
                   err.message);
      heif_context_free (ctx);

      return NULL;
    }

  has_alpha = heif_image_handle_has_alpha_channel (handle);

  err = heif_decode_image (handle,
                           &img,
                           heif_colorspace_RGB,
                           has_alpha ? heif_chroma_interleaved_RGBA :
                           heif_chroma_interleaved_RGB,
                           NULL);
  if (err.code)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Loading HEIF image failed: %s"),
                   err.message);
     heif_image_handle_release (handle);
     heif_context_free (ctx);

     return NULL;
    }

#ifdef HAVE_LIBHEIF_1_4_0
  switch (heif_image_handle_get_color_profile_type (handle))
    {
    case heif_color_profile_type_not_present:
      break;
    case heif_color_profile_type_rICC:
    case heif_color_profile_type_prof:
      /* I am unsure, but it looks like both these types represent an
       * ICC color profile. XXX
       */
        {
          void   *profile_data;
          size_t  profile_size;

          profile_size = heif_image_handle_get_raw_color_profile_size (handle);
          profile_data = g_malloc0 (profile_size);
          err = heif_image_handle_get_raw_color_profile (handle, profile_data);

          if (err.code)
            g_warning ("%s: color profile loading failed and discarded.",
                       G_STRFUNC);
          else
            profile = gimp_color_profile_new_from_icc_profile ((guint8 *) profile_data,
                                                               profile_size, NULL);

          g_free (profile_data);
        }
      break;

    default:
      /* heif_color_profile_type_nclx (what is that?) and any future
       * profile type which we don't support in GIMP (yet).
       */
      g_warning ("%s: unknown color profile type has been discarded.",
                 G_STRFUNC);
      break;
    }
#endif /* HAVE_LIBHEIF_1_4_0 */

  gimp_progress_update (0.75);

  width  = heif_image_get_width  (img, heif_channel_interleaved);
  height = heif_image_get_height (img, heif_channel_interleaved);

  /* create GIMP image and copy HEIF image into the GIMP image
   * (converting it to RGB)
   */

  image = gimp_image_new (width, height, GIMP_RGB);
  gimp_image_set_file (image, file);

  if (profile)
    gimp_image_set_color_profile (image, profile);

  layer = gimp_layer_new (image,
                          _("image content"),
                          width, height,
                          has_alpha ? GIMP_RGBA_IMAGE : GIMP_RGB_IMAGE,
                          100.0,
                          gimp_image_get_default_new_layer_mode (image));

  gimp_image_insert_layer (image, layer, NULL, 0);

  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));

  if (has_alpha)
    format = babl_format_with_space ("R'G'B'A u8",
                                     gegl_buffer_get_format (buffer));
  else
    format = babl_format_with_space ("R'G'B' u8",
                                     gegl_buffer_get_format (buffer));

  data = heif_image_get_plane_readonly (img, heif_channel_interleaved,
                                        &stride);

  gegl_buffer_set (buffer,
                   GEGL_RECTANGLE (0, 0, width, height),
                   0, format, data, stride);

  g_object_unref (buffer);

  {
    size_t        exif_data_size = 0;
    uint8_t      *exif_data      = NULL;
    size_t        xmp_data_size  = 0;
    uint8_t      *xmp_data       = NULL;
    gint          n_metadata;
    heif_item_id  metadata_id;

    n_metadata =
      heif_image_handle_get_list_of_metadata_block_IDs (handle,
                                                        "Exif",
                                                        &metadata_id, 1);
    if (n_metadata > 0)
      {
        exif_data_size = heif_image_handle_get_metadata_size (handle,
                                                              metadata_id);
        exif_data = g_alloca (exif_data_size);

        err = heif_image_handle_get_metadata (handle, metadata_id, exif_data);
        if (err.code != 0)
          {
            exif_data      = NULL;
            exif_data_size = 0;
          }
      }

    n_metadata =
      heif_image_handle_get_list_of_metadata_block_IDs (handle,
                                                        "XMP",
                                                        &metadata_id, 1);
    if (n_metadata > 0)
      {
        xmp_data_size = heif_image_handle_get_metadata_size (handle,
                                                             metadata_id);
        xmp_data = g_alloca (xmp_data_size);

        err = heif_image_handle_get_metadata (handle, metadata_id, xmp_data);
        if (err.code != 0)
          {
            xmp_data      = NULL;
            xmp_data_size = 0;
          }
      }

    if (exif_data || xmp_data)
      {
        GimpMetadata          *metadata = gimp_metadata_new ();
        GimpMetadataLoadFlags  flags    = GIMP_METADATA_LOAD_ALL;

        if (exif_data)
          gimp_metadata_set_from_exif (metadata,
                                       exif_data, exif_data_size, NULL);

        if (xmp_data)
          gimp_metadata_set_from_xmp (metadata,
                                      xmp_data, xmp_data_size, NULL);

        if (profile)
          flags &= ~GIMP_METADATA_LOAD_COLORSPACE;

        gimp_image_metadata_load_finish (image, "image/heif",
                                         metadata, flags,
                                         interactive);
      }
  }

  if (profile)
    g_object_unref (profile);

  heif_image_handle_release (handle);
  heif_context_free (ctx);
  heif_image_release (img);

  gimp_progress_update (1.0);

  return image;
}

static struct heif_error
write_callback (struct heif_context *ctx,
                const void          *data,
                size_t               size,
                void                *userdata)
{
  GOutputStream     *output = userdata;
  GError            *error  = NULL;
  struct heif_error  heif_error;

  heif_error.code    = heif_error_Ok;
  heif_error.subcode = heif_error_Ok;
  heif_error.message = "";

  if (! g_output_stream_write_all (output, data, size, NULL, NULL, &error))
    {
      heif_error.code    = 99; /* hmm */
      heif_error.message = error->message;
    }

  return heif_error;
}

static gboolean
save_image (GFile         *file,
            GimpImage     *image,
            GimpDrawable  *drawable,
            GObject       *config,
            GError       **error)
{
  struct heif_image        *h_image = NULL;
  struct heif_context      *context = heif_context_alloc ();
  struct heif_encoder      *encoder = NULL;
  struct heif_image_handle *handle  = NULL;
  struct heif_writer        writer;
  struct heif_error         err;
  GOutputStream            *output;
  GeglBuffer               *buffer;
  const gchar              *encoding;
  const Babl               *format;
  const Babl               *space   = NULL;
  guint8                   *data;
  gint                      stride;
  gint                      width;
  gint                      height;
  gboolean                  has_alpha;
  gboolean                  out_linear = FALSE;
  gboolean                  lossless;
  gint                      quality;
  gboolean                  save_profile;

  g_object_get (config,
                "lossless",           &lossless,
                "quality",            &quality,
                "save-color-profile", &save_profile,
                NULL);

  gimp_progress_init_printf (_("Exporting '%s'"),
                             g_file_get_parse_name (file));

  width   = gimp_drawable_width  (drawable);
  height  = gimp_drawable_height (drawable);

  has_alpha = gimp_drawable_has_alpha (drawable);

  err = heif_image_create (width, height,
                           heif_colorspace_RGB,
                           has_alpha ?
                           heif_chroma_interleaved_RGBA :
                           heif_chroma_interleaved_RGB,
                           &h_image);

#ifdef HAVE_LIBHEIF_1_4_0
  if (save_profile)
    {
      GimpColorProfile *profile = NULL;
      const guint8     *icc_data;
      gsize             icc_length;

      profile = gimp_image_get_color_profile (image);
      if (profile && gimp_color_profile_is_linear (profile))
        out_linear = TRUE;

      if (! profile)
        {
          profile = gimp_image_get_effective_color_profile (image);

          if (gimp_color_profile_is_linear (profile))
            {
              if (gimp_image_get_precision (image) != GIMP_PRECISION_U8_LINEAR)
                {
                  /* If stored data was linear, let's convert the profile. */
                  GimpColorProfile *saved_profile;

                  saved_profile = gimp_color_profile_new_srgb_trc_from_color_profile (profile);
                  g_clear_object (&profile);
                  profile = saved_profile;
                }
              else
                {
                  /* Keep linear profile as-is for 8-bit linear image. */
                  out_linear = TRUE;
                }
            }
        }

      icc_data = gimp_color_profile_get_icc_profile (profile, &icc_length);
      heif_image_set_raw_color_profile (h_image, "prof", icc_data, icc_length);
      space = gimp_color_profile_get_space (profile,
                                            GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                            error);
      if (error && *error)
        {
          /* Don't make this a hard failure yet output the error. */
          g_printerr ("%s: error getting the profile space: %s",
                      G_STRFUNC, (*error)->message);
          g_clear_error (error);
        }

      g_object_unref (profile);
    }
#endif /* HAVE_LIBHEIF_1_4_0 */

  if (! space)
    space = gimp_drawable_get_format (drawable);

  heif_image_add_plane (h_image, heif_channel_interleaved,
                        width, height, has_alpha ? 32 : 24);

  data = heif_image_get_plane (h_image, heif_channel_interleaved, &stride);

  buffer = gimp_drawable_get_buffer (drawable);

  if (has_alpha)
    {
      if (out_linear)
        encoding = "RGBA u8";
      else
        encoding = "R'G'B'A u8";
    }
  else
    {
      if (out_linear)
        encoding = "RGB u8";
      else
        encoding = "R'G'B' u8";
    }
  format = babl_format_with_space (encoding, space);

  gegl_buffer_get (buffer,
                   GEGL_RECTANGLE (0, 0, width, height),
                   1.0, format, data, stride, GEGL_ABYSS_NONE);

  g_object_unref (buffer);

  gimp_progress_update (0.33);

  /*  encode to HEIF file  */

  err = heif_context_get_encoder_for_format (context,
                                             heif_compression_HEVC,
                                             &encoder);

  heif_encoder_set_lossy_quality (encoder, quality);
  heif_encoder_set_lossless (encoder, lossless);
  /* heif_encoder_set_logging_level (encoder, logging_level); */

  err = heif_context_encode_image (context,
                                   h_image,
                                   encoder,
                                   NULL,
                                   &handle);
  if (err.code != 0)
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Encoding HEIF image failed: %s"),
                   err.message);
      return FALSE;
    }

  heif_image_handle_release (handle);

  gimp_progress_update (0.66);

  writer.writer_api_version = 1;
  writer.write              = write_callback;

  output = G_OUTPUT_STREAM (g_file_replace (file,
                                            NULL, FALSE, G_FILE_CREATE_NONE,
                                            NULL, error));
  if (! output)
    return FALSE;

  err = heif_context_write (context, &writer, output);

  if (err.code != 0)
    {
      GCancellable *cancellable = g_cancellable_new ();

      g_cancellable_cancel (cancellable);
      g_output_stream_close (output, cancellable, NULL);
      g_object_unref (cancellable);

      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                   _("Writing HEIF image failed: %s"),
                   err.message);
      return FALSE;
    }

  g_object_unref (output);

  heif_context_free (context);
  heif_image_release (h_image);

  heif_encoder_release (encoder);

  gimp_progress_update (1.0);

  return TRUE;
}


/*  the load dialog  */

#define MAX_THUMBNAIL_SIZE 320

typedef struct _HeifImage HeifImage;

struct _HeifImage
{
  uint32_t           ID;
  gchar              caption[100];
  struct heif_image *thumbnail;
  gint               width;
  gint               height;
};

static gboolean
load_thumbnails (struct heif_context *heif,
                 HeifImage           *images)
{
  guint32 *IDs;
  gint     n_images;
  gint     i;

  n_images = heif_context_get_number_of_top_level_images (heif);

  /* get list of all (top level) image IDs */

  IDs = g_alloca (n_images * sizeof (guint32));

  heif_context_get_list_of_top_level_image_IDs (heif, IDs, n_images);


  /* Load a thumbnail for each image. */

  for (i = 0; i < n_images; i++)
    {
      struct heif_image_handle *handle = NULL;
      struct heif_error         err;
      gint                      width;
      gint                      height;
      struct heif_image_handle *thumbnail_handle = NULL;
      heif_item_id              thumbnail_ID;
      gint                      n_thumbnails;
      struct heif_image        *thumbnail_img = NULL;
      gint                      thumbnail_width;
      gint                      thumbnail_height;

      images[i].ID         = IDs[i];
      images[i].caption[0] = 0;
      images[i].thumbnail  = NULL;

      /* get image handle */

      err = heif_context_get_image_handle (heif, IDs[i], &handle);
      if (err.code)
        {
          gimp_message (err.message);
          continue;
        }

      /* generate image caption */

      width  = heif_image_handle_get_width  (handle);
      height = heif_image_handle_get_height (handle);

      if (heif_image_handle_is_primary_image (handle))
        {
          g_snprintf (images[i].caption, sizeof (images[i].caption),
                      "%dx%d (%s)", width, height, _("primary"));
        }
      else
        {
          g_snprintf (images[i].caption, sizeof (images[i].caption),
                      "%dx%d", width, height);
        }

      /* get handle to thumbnail image
       *
       * if there is no thumbnail image, just the the image itself
       * (will be scaled down later)
       */

      n_thumbnails = heif_image_handle_get_list_of_thumbnail_IDs (handle,
                                                                  &thumbnail_ID,
                                                                  1);

      if (n_thumbnails > 0)
        {
          err = heif_image_handle_get_thumbnail (handle, thumbnail_ID,
                                                 &thumbnail_handle);
          if (err.code)
            {
              gimp_message (err.message);
              continue;
            }
        }
      else
        {
          err = heif_context_get_image_handle (heif, IDs[i], &thumbnail_handle);
          if (err.code)
            {
              gimp_message (err.message);
              continue;
            }
        }

      /* decode the thumbnail image */

      err = heif_decode_image (thumbnail_handle,
                               &thumbnail_img,
                               heif_colorspace_RGB,
                               heif_chroma_interleaved_RGB,
                               NULL);
      if (err.code)
        {
          gimp_message (err.message);
          continue;
        }

      /* if thumbnail image size exceeds the maximum, scale it down */

      thumbnail_width  = heif_image_handle_get_width  (thumbnail_handle);
      thumbnail_height = heif_image_handle_get_height (thumbnail_handle);

      if (thumbnail_width  > MAX_THUMBNAIL_SIZE ||
          thumbnail_height > MAX_THUMBNAIL_SIZE)
        {
          /* compute scaling factor to fit into a max sized box */

          gfloat factor_h = thumbnail_width  / (gfloat) MAX_THUMBNAIL_SIZE;
          gfloat factor_v = thumbnail_height / (gfloat) MAX_THUMBNAIL_SIZE;
          gint   new_width, new_height;
          struct heif_image *scaled_img = NULL;

          if (factor_v > factor_h)
            {
              new_height = MAX_THUMBNAIL_SIZE;
              new_width  = thumbnail_width / factor_v;
            }
          else
            {
              new_height = thumbnail_height / factor_h;
              new_width  = MAX_THUMBNAIL_SIZE;
            }

          /* scale the image */

          err = heif_image_scale_image (thumbnail_img,
                                        &scaled_img,
                                        new_width, new_height,
                                        NULL);
          if (err.code)
            {
              gimp_message (err.message);
              continue;
            }

          /* release the old image and only keep the scaled down version */

          heif_image_release (thumbnail_img);
          thumbnail_img = scaled_img;

          thumbnail_width  = new_width;
          thumbnail_height = new_height;
        }

      heif_image_handle_release (thumbnail_handle);
      heif_image_handle_release (handle);

      /* remember the HEIF thumbnail image (we need it for the GdkPixbuf) */

      images[i].thumbnail = thumbnail_img;

      images[i].width  = thumbnail_width;
      images[i].height = thumbnail_height;
    }

  return TRUE;
}

static void
load_dialog_item_activated (GtkIconView *icon_view,
                            GtkTreePath *path,
                            GtkDialog   *dialog)
{
  gtk_dialog_response (dialog, GTK_RESPONSE_OK);
}

static gboolean
load_dialog (struct heif_context *heif,
             uint32_t            *selected_image)
{
  GtkWidget       *dialog;
  GtkWidget       *main_vbox;
  GtkWidget       *frame;
  HeifImage       *heif_images;
  GtkListStore    *list_store;
  GtkTreeIter      iter;
  GtkWidget       *scrolled_window;
  GtkWidget       *icon_view;
  GtkCellRenderer *renderer;
  gint             n_images;
  gint             i;
  gint             selected_idx = -1;
  gboolean         run          = FALSE;

  n_images = heif_context_get_number_of_top_level_images (heif);

  heif_images = g_alloca (n_images * sizeof (HeifImage));

  if (! load_thumbnails (heif, heif_images))
    return FALSE;

  dialog = gimp_dialog_new (_("Load HEIF Image"), PLUG_IN_BINARY,
                            NULL, 0,
                            gimp_standard_help_func, LOAD_PROC,

                            _("_Cancel"), GTK_RESPONSE_CANCEL,
                            _("_OK"),     GTK_RESPONSE_OK,

                            NULL);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      main_vbox, TRUE, TRUE, 0);

  frame = gimp_frame_new (_("Select Image"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  /* prepare list store with all thumbnails and caption */

  list_store = gtk_list_store_new (2, G_TYPE_STRING, GDK_TYPE_PIXBUF);

  for (i = 0; i < n_images; i++)
    {
      GdkPixbuf    *pixbuf;
      const guint8 *data;
      gint          stride;

      gtk_list_store_append (list_store, &iter);
      gtk_list_store_set (list_store, &iter, 0, heif_images[i].caption, -1);

      data = heif_image_get_plane_readonly (heif_images[i].thumbnail,
                                            heif_channel_interleaved,
                                            &stride);

      pixbuf = gdk_pixbuf_new_from_data (data,
                                         GDK_COLORSPACE_RGB,
                                         FALSE,
                                         8,
                                         heif_images[i].width,
                                         heif_images[i].height,
                                         stride,
                                         NULL,
                                         NULL);

      gtk_list_store_set (list_store, &iter, 1, pixbuf, -1);
    }

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                       GTK_SHADOW_IN);
  gtk_widget_set_size_request (scrolled_window,
                               2   * MAX_THUMBNAIL_SIZE,
                               1.5 * MAX_THUMBNAIL_SIZE);
  gtk_container_add (GTK_CONTAINER (frame), scrolled_window);
  gtk_widget_show (scrolled_window);

  icon_view = gtk_icon_view_new_with_model (GTK_TREE_MODEL (list_store));
  gtk_container_add (GTK_CONTAINER (scrolled_window), icon_view);
  gtk_widget_show (icon_view);

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (icon_view), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (icon_view), renderer,
                                  "pixbuf", 1,
                                  NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (icon_view), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (icon_view), renderer,
                                  "text", 0,
                                  NULL);
  g_object_set (renderer,
                "alignment", PANGO_ALIGN_CENTER,
                "wrap-mode", PANGO_WRAP_WORD_CHAR,
                "xalign",    0.5,
                "yalign",    0.0,
                NULL);

  g_signal_connect (icon_view, "item-activated",
                    G_CALLBACK (load_dialog_item_activated),
                    dialog);

  /* pre-select the primary image */

  for (i = 0; i < n_images; i++)
    {
      if (heif_images[i].ID == *selected_image)
        {
          selected_idx = i;
          break;
        }
    }

  if (selected_idx != -1)
    {
      GtkTreePath *path = gtk_tree_path_new_from_indices (selected_idx, -1);

      gtk_icon_view_select_path (GTK_ICON_VIEW (icon_view), path);
      gtk_tree_path_free (path);
    }

  gtk_widget_show (main_vbox);
  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  if (run)
    {
      GList *selected_items =
        gtk_icon_view_get_selected_items (GTK_ICON_VIEW (icon_view));

      if (selected_items)
        {
          GtkTreePath *path    = selected_items->data;
          gint        *indices = gtk_tree_path_get_indices (path);

          *selected_image = heif_images[indices[0]].ID;

          g_list_free_full (selected_items,
                            (GDestroyNotify) gtk_tree_path_free);
        }
    }

  gtk_widget_destroy (dialog);

  /* release thumbnail images */

  for (i = 0 ; i < n_images; i++)
    heif_image_release (heif_images[i].thumbnail);

  return run;
}


/*  the save dialog  */

gboolean
save_dialog (GimpProcedure *procedure,
             GObject       *config)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *grid;
  GtkWidget *button;
  GtkWidget *frame;
  gboolean   run;

  dialog = gimp_procedure_dialog_new (procedure,
                                      GIMP_PROCEDURE_CONFIG (config),
                                      _("Export Image as HEIF"));

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
                      main_vbox, FALSE, FALSE, 0);
  gtk_widget_show (main_vbox);

  frame = gimp_frame_new (NULL);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  button = gimp_prop_check_button_new (config, "lossless",
                                       _("_Lossless"));
  gtk_frame_set_label_widget (GTK_FRAME (frame), button);

  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  gtk_container_add (GTK_CONTAINER (frame), grid);
  gtk_widget_show (grid);

  g_object_bind_property (config, "lossless",
                          grid,   "sensitive",
                          G_BINDING_SYNC_CREATE |
                          G_BINDING_INVERT_BOOLEAN);

  gimp_prop_scale_entry_new (config, "quality",
                             GTK_GRID (grid), 0, 1,
                             _("_Quality"),
                             1, 10, 0,
                             FALSE, 0, 0);

#ifdef HAVE_LIBHEIF_1_4_0
  button = gimp_prop_check_button_new (config, "save-color-profile",
                                       _("Save color _profile"));
  gtk_box_pack_start (GTK_BOX (main_vbox), button, FALSE, FALSE, 0);
#endif

  gtk_widget_show (dialog);

  run = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));

  gtk_widget_destroy (dialog);

  return run;
}
