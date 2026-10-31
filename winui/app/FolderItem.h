#pragma once

#include "FolderItem.g.h"

namespace winrt::EvolutionWin::implementation
{
    struct FolderItem : FolderItemT<FolderItem>
    {
        FolderItem(hstring const& name, hstring const& glyph, int32_t unread,
                   hstring const& account, hstring const& folder, bool is_account)
            : m_name(name), m_glyph(glyph), m_unread(unread),
              m_account(account), m_folder(folder), m_is_account(is_account)
        {
        }

        hstring Name() const { return m_name; }
        hstring Glyph() const { return m_glyph; }
        int32_t Unread() const { return m_unread; }
        void Unread(int32_t value)
        {
            if (m_unread == value)
                return;

            m_unread = value;
            m_property_changed(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ L"Unread" });
            m_property_changed(*this, Microsoft::UI::Xaml::Data::PropertyChangedEventArgs{ L"UnreadText" });
        }
        hstring UnreadText() const { return m_unread > 0 ? to_hstring(m_unread) : hstring{}; }
        hstring Account() const { return m_account; }
        hstring Folder() const { return m_folder; }
        bool IsAccount() const { return m_is_account; }

        event_token PropertyChanged(Microsoft::UI::Xaml::Data::PropertyChangedEventHandler const& handler)
        {
            return m_property_changed.add(handler);
        }
        void PropertyChanged(event_token const& token) noexcept
        {
            m_property_changed.remove(token);
        }

    private:
        hstring m_name;
        hstring m_glyph;
        int32_t m_unread;
        hstring m_account;
        hstring m_folder;
        bool m_is_account;
        event<Microsoft::UI::Xaml::Data::PropertyChangedEventHandler> m_property_changed;
    };
}

namespace winrt::EvolutionWin::factory_implementation
{
    struct FolderItem : FolderItemT<FolderItem, implementation::FolderItem>
    {
    };
}
