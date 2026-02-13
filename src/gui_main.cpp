#include <gtkmm.h>
#include <adwaita.h>
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>s

class SimpleCompositor : public Gtk::ApplicationWindow {
public:
    SimpleCompositor(const Glib::RefPtr<Gtk::Application>& app)
    : Gtk::ApplicationWindow(app) {
        set_title("Deep Compositor Demo");
        set_default_size(1100, 700);

        // Dark theme initialization
        auto manager = adw_style_manager_get_default();
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_PREFER_DARK);

        auto main_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        set_child(*main_hbox);

        // --- SIDEBAR ---
        auto sidebar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 20);
        sidebar->set_margin_top(15);
        sidebar->set_margin_bottom(15);
        sidebar->set_margin_start(15);
        sidebar->set_margin_end(15);
        
        // Locking the sidebar width
        sidebar->set_size_request(300, -1);
        sidebar->set_hexpand(false);

        auto title = Gtk::make_managed<Gtk::Label>("COMPOSITION SETUP");
        title->add_css_class("title-1"); // Adwaita styling
        sidebar->append(*title);

        for (int i = 0; i < 3; ++i) {
            auto slot_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);

            auto label = Gtk::make_managed<Gtk::Label>("Input Layer " + std::to_string(i + 1));
            label->set_halign(Gtk::Align::START);
            slot_box->append(*label);

            // Path Row
            auto path_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
            auto entry = Gtk::make_managed<Gtk::Entry>();
            entry->set_hexpand(true);
            entry->set_placeholder_text("Select .exr file...");
            path_hbox->append(*entry);
            
            auto browse_btn = Gtk::make_managed<Gtk::Button>();
            browse_btn->set_icon_name("folder-open-symbolic");
            path_hbox->append(*browse_btn);
            slot_box->append(*path_hbox);

            // Z-Offset Row
            auto z_hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
            z_hbox->append(*Gtk::make_managed<Gtk::Label>("Z Offset:"));
            auto z_input = Gtk::make_managed<Gtk::SpinButton>(
                Gtk::Adjustment::create(0.0, -100.0, 100.0, 0.5, 1.0, 0.0), 0.5, 1);
            z_hbox->append(*z_input);
            slot_box->append(*z_hbox);

            sidebar->append(*slot_box);

            input_entries.push_back(entry);
            z_offsets.push_back(z_input);

            browse_btn->signal_clicked().connect([this, entry]() {
                auto dialog = Gtk::FileChooserNative::create("Select EXR", *this, Gtk::FileChooser::Action::OPEN);
                dialog->signal_response().connect([this, dialog, entry](int response) {
                    if (response == (int)Gtk::ResponseType::ACCEPT) {
                        entry->set_text(dialog->get_file()->get_path());
                    }
                });
                dialog->show();
            });
        }

        auto comp_btn = Gtk::make_managed<Gtk::Button>("GENERATE COMPOSITE");
        comp_btn->set_margin_top(20);
        comp_btn->add_css_class("suggested-action"); // Blue button
        comp_btn->set_size_request(-1, 50);
        comp_btn->signal_clicked().connect(sigc::mem_fun(*this, &SimpleCompositor::on_composite_clicked));
        sidebar->append(*comp_btn);

        main_hbox->append(*sidebar);

        // --- SEPARATOR ---
        main_hbox->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));

        // --- VIEWPORT ---
        viewport = Gtk::make_managed<Gtk::Picture>();
        viewport->set_hexpand(true);
        viewport->set_vexpand(true);
        viewport->set_content_fit(Gtk::ContentFit::CONTAIN);
        viewport->set_alternative_text("Composite output will appear here");
        main_hbox->append(*viewport);
    }

protected:
    std::vector<Gtk::Entry*> input_entries;
    std::vector<Gtk::SpinButton*> z_offsets;
    Gtk::Picture* viewport;

    void on_composite_clicked() {

        std::system("mkdir -p output");


        std::string final_output = "output/gui_composite";
        
        std::string current_dir = "./";


        // Note: tool creates .png. We'll use a specific filename for the demo.
        
        // Build Command string
        // Logic: ./deep_compositor --deep-output input1.exr input2.exr input3.exr output_name
        std::string cmd = current_dir + "deep_compositor --deep-output --mod-offset ";

        for (size_t i = 0; i < input_entries.size(); ++i) {
            std::string path = input_entries[i]->get_text();
            if (path.empty()) {
                std::cerr << "Error: Please select all 3 input files first." << std::endl;
                return;
            }

            // Get corresponding Z offset value
            double z_val = z_offsets[i]->get_value();

            // Append to command: "path" z_value
            // Example: "image.exr" 5.0 
            cmd += "\"" + path + "\" " + std::to_string(z_val) + " ";
        }
        
        std::string output_base = "output/gui_composite";
        cmd += "\"" + final_output + "\"";

        // std::cout << "Shell Execution: " << cmd << std::endl;
        std::cout << "Attempting to execute: " << cmd << std::endl;
        
        // Execute the CLI tool
        int result = std::system(cmd.c_str());

        if (result == 0) {
            // Update UI with result (tool adds .png to the output base)
            std::cout << "Displaying file " << cmd << std::endl;
            auto file = Gio::File::create_for_path(output_base + ".png");
            viewport->set_file(file);
        } else {
            std::cerr << "Command failed. Try running this manually in terminal to see the error:" << std::endl;
            std::cerr << cmd << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    adw_init();
    auto app = Gtk::Application::create("com.demo.compositor");
    
    app->signal_activate().connect([app]() {
        auto win = new SimpleCompositor(app);
        win->present();
    });

    return app->run(argc, argv);
}