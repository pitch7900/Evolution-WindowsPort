#pragma once

/* Photo d'un expéditeur dans l'en-tête d'un message (pastille ronde, comme dans Outlook) :
 * décodage de l'URI de données renvoyée par le moteur (evo_core_get_sender_photo) et
 * affichage dans un PersonPicture. Sans photo, le PersonPicture affiche les initiales
 * du nom (DisplayName). Partagé par la fenêtre principale et la fenêtre de message. */

#include <string_view>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

namespace EvolutionWin::SenderPhoto
{
    /** Adresse électronique d'un expéditeur « Nom <adresse> » ou « adresse » (la première s'il y en a
     *  plusieurs) ; vide si aucune adresse valable.
     *  @param from Champ From tel que fourni par le moteur. */
    inline std::wstring AddressOf(winrt::hstring const& from)
    {
        std::wstring_view view{ from };
        std::wstring_view address;

        auto open = view.find(L'<');
        if (open != std::wstring_view::npos)
        {
            auto close = view.find(L'>', open);
            address = view.substr(open + 1, close == std::wstring_view::npos ? std::wstring_view::npos : close - open - 1);
        }
        else
        {
            address = view.substr(0, view.find(L','));
        }

        while (!address.empty() && iswspace(address.front()))
            address.remove_prefix(1);
        while (!address.empty() && iswspace(address.back()))
            address.remove_suffix(1);

        auto at = address.rfind(L'@');
        if (at == std::wstring_view::npos || at == 0 || at + 1 >= address.size() ||
            address.find_first_of(L" \t\"<>,;") != std::wstring_view::npos)
            return {};

        return std::wstring{ address };
    }

    /** Affiche la photo reçue du moteur dans la pastille ; sans photo valable, la pastille garde les initiales.
     *  @param picture  Pastille de l'en-tête.
     *  @param data_uri URI de données "data:image/...;base64,..." (vide si aucune photo). */
    inline void Apply(winrt::Microsoft::UI::Xaml::Controls::PersonPicture const& picture, winrt::hstring const& data_uri)
    {
        constexpr std::wstring_view prefix = L"base64,";
        std::wstring_view text{ data_uri };
        auto at = text.find(prefix);
        if (at == std::wstring_view::npos)
            return;

        try
        {
            auto buffer = winrt::Windows::Security::Cryptography::CryptographicBuffer::DecodeFromBase64String(
                winrt::hstring{ text.substr(at + prefix.size()) });
            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            winrt::Windows::Storage::Streams::DataWriter writer{ stream };
            writer.WriteBuffer(buffer);
            writer.StoreAsync().get();
            writer.DetachStream();
            stream.Seek(0);

            winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap;
            bitmap.SetSource(stream);
            picture.ProfilePicture(bitmap);
        }
        catch (...)
        {
            /* image illisible : les initiales restent affichées */
        }
    }
}
